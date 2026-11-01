#include "ui_panel.h"
#include "eyes_panel.h"
#include "lvgl.h"
#include <math.h>

#define FADE_STRIP_ROWS 22
#define FADE_TIMER_MS 15

static inline uint16_t swap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

/* RGB565 lerp, alpha in 0..32 (exact at both ends), all three channels in one 32-bit multiply. */
static inline uint16_t blend565(uint16_t bg, uint16_t fg, uint32_t alpha32)
{
    uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81Fu;
    uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81Fu;
    uint32_t r = ((((f - b) * alpha32) >> 5) + b) & 0x07E0F81Fu;
    return (uint16_t)((r >> 16) | r);
}

bool ui_panel_strips_init(ui_panel_strips_t *strips, size_t pixels)
{
    strips->pixels = pixels;
    strips->next = 0;
    strips->strip[0] = eyes_panel_alloc_dma(pixels * sizeof(uint16_t));
    strips->strip[1] = eyes_panel_alloc_dma(pixels * sizeof(uint16_t));
    if (!strips->strip[0] || !strips->strip[1]) {
        ui_panel_strips_free(strips);
        return false;
    }
    return true;
}

void ui_panel_strips_free(ui_panel_strips_t *strips)
{
    for (int i = 0; i < 2; i++) {
        if (strips->strip[i]) {
            eyes_panel_free_dma(strips->strip[i]);
            strips->strip[i] = NULL;
        }
    }
}

void ui_panel_send(ui_panel_strips_t *strips, const uint16_t *image, int x0, int y0, int x1, int y1)
{
    x0 = LV_MAX(0, x0) & ~1;
    y0 = LV_MAX(0, y0) & ~1;
    x1 = LV_MIN(UI_PANEL_SIZE, (x1 + 1) & ~1);
    y1 = LV_MIN(UI_PANEL_SIZE, (y1 + 1) & ~1);
    int width = x1 - x0;
    if (width <= 0 || y1 <= y0 || !strips->strip[0]) {
        return;
    }
    int rows = LV_MAX(2, (int)(strips->pixels / (size_t)width) & ~1);
    for (int y = y0; y < y1; y += rows) {
        int y_end = LV_MIN(y1, y + rows);
        uint16_t *buf = strips->strip[strips->next];
        uint16_t *out = buf;
        for (int row = y; row < y_end; row++) {
            const uint16_t *src = image + (size_t)row * UI_PANEL_SIZE + x0;
            for (int x = 0; x < width; x++) {
                *out++ = swap16(src[x]);
            }
        }
        /* Filling the next strip overlaps this one's DMA; only the last transfer is waited for. */
        eyes_panel_blit(x0, y, x1, y_end, buf, y_end >= y1);
        strips->next ^= 1;
    }
}

static struct {
    lv_timer_t *timer;
    ui_panel_strips_t strips;
    const uint16_t *from;
    const uint16_t *to;
    uint32_t start;
    uint32_t duration;
    void (*done)(void *ctx);
    void *ctx;
} s_fade;

static float smootherstep(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/* The round panel hides its corners: send only the span of each strip the circle touches. */
static void visible_span(int y0, int y1, int *x0, int *x1)
{
    const float c = UI_PANEL_SIZE / 2.0f;
    float dy = 0.0f;
    if ((float)y1 <= c) {
        dy = c - (float)y1;
    } else if ((float)y0 >= c) {
        dy = (float)y0 - c;
    }
    float half = sqrtf(LV_MAX(0.0f, c * c - dy * dy)) + 2.0f;
    *x0 = LV_MAX(0, (int)(c - half)) & ~1;
    *x1 = LV_MIN(UI_PANEL_SIZE, ((int)ceilf(c + half) + 1) & ~1);
}

static const uint16_t s_black_row[UI_PANEL_SIZE] __attribute__((aligned(4)));

/* Two pixels per step with 32-bit PSRAM loads: the blend, not the transfer, limits the fade rate. */
static void blend_row(uint32_t *out, const uint32_t *a, const uint32_t *b, int pairs, uint32_t alpha32)
{
    for (int k = 0; k < pairs; k++) {
        uint32_t pa = a[k], pb = b[k];
        uint16_t lo = blend565((uint16_t)pa, (uint16_t)pb, alpha32);
        uint16_t hi = blend565((uint16_t)(pa >> 16), (uint16_t)(pb >> 16), alpha32);
        out[k] = (uint32_t)swap16(lo) | ((uint32_t)swap16(hi) << 16);
    }
}

typedef struct {
    uint16_t *out;
    const uint16_t *from;
    const uint16_t *to;
    int y0, y1, strip_y, x0, width;
    uint32_t alpha32;
} blend_job_t;

static void blend_rows(void *arg)
{
    const blend_job_t *job = arg;
    for (int row = job->y0; row < job->y1; row++) {
        size_t offset = (size_t)row * UI_PANEL_SIZE + job->x0;
        const uint16_t *a = job->from ? job->from + offset : s_black_row;
        const uint16_t *b = job->to ? job->to + offset : s_black_row;
        blend_row((uint32_t *)(job->out + (row - job->strip_y) * job->width), (const uint32_t *)a,
                  (const uint32_t *)b, job->width / 2, job->alpha32);
    }
}

void ui_panel_blend(ui_panel_strips_t *strips, const uint16_t *from, const uint16_t *to, uint32_t alpha32)
{
    int rows = (int)(strips->pixels / UI_PANEL_SIZE) & ~1;
    for (int y = 0; y < UI_PANEL_SIZE; y += rows) {
        int y_end = LV_MIN(UI_PANEL_SIZE, y + rows);
        int x0, x1;
        visible_span(y, y_end, &x0, &x1);
        int width = x1 - x0;
        uint16_t *out = strips->strip[strips->next];
        /* Both cores blend: the helper takes the lower half of the strip. */
        int mid = y + (y_end - y) / 2;
        blend_job_t helper = { out, from, to, mid, y_end, y, x0, width, alpha32 };
        blend_job_t own = { out, from, to, y, mid, y, x0, width, alpha32 };
        eyes_panel_run_async(blend_rows, &helper);
        blend_rows(&own);
        eyes_panel_join();
        eyes_panel_blit(x0, y, x1, y_end, out, y_end >= UI_PANEL_SIZE);
        strips->next ^= 1;
    }
}

static void fade_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    float t = LV_MIN(1.0f, (float)lv_tick_elaps(s_fade.start) / (float)s_fade.duration);
    ui_panel_blend(&s_fade.strips, s_fade.from, s_fade.to, (uint32_t)lroundf(smootherstep(t) * 32.0f));
    if (t < 1.0f) {
        return;
    }
    lv_timer_delete(s_fade.timer);
    s_fade.timer = NULL;
    ui_panel_strips_free(&s_fade.strips);
    void (*done)(void *ctx) = s_fade.done;
    void *ctx = s_fade.ctx;
    s_fade.done = NULL;
    if (done) {
        done(ctx);
    }
    eyes_panel_end();
}

bool ui_panel_fade_active(void)
{
    return s_fade.timer != NULL;
}

bool ui_panel_fade(const uint16_t *from, const uint16_t *to, uint32_t duration_ms,
                   void (*done)(void *ctx), void *ctx)
{
    if (s_fade.timer || !eyes_panel_begin()) {
        return false;
    }
    if (!ui_panel_strips_init(&s_fade.strips, UI_PANEL_SIZE * FADE_STRIP_ROWS)) {
        eyes_panel_end();
        return false;
    }
    s_fade.from = from;
    s_fade.to = to;
    s_fade.duration = LV_MAX(duration_ms, 1u);
    s_fade.done = done;
    s_fade.ctx = ctx;
    s_fade.start = lv_tick_get();
    s_fade.timer = lv_timer_create(fade_timer_cb, FADE_TIMER_MS, NULL);
    return true;
}
