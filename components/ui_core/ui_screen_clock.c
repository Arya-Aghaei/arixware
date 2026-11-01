#include "ui_screen_clock.h"
#include "ui_orient.h"
#include "images.h"
#include "fonts.h"
#include "esp_heap_caps.h"
#include <time.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

#define CLOCK_UPDATE_MS 1000
#define FACE_SIZE 466
/* Centre the clock content on the sunrise background. */
#define FACE_CX 233
#define FACE_CY 233

/* "Cute Cosmic Robot" palette, matching the bundled background art. */
#define COLOR_TEXT      0xFFF4DF /* center time */
#define COLOR_SECONDARY 0xFFD49B /* seconds, date */
#define COLOR_PRIMARY   0xFF9347 /* glow */

/* The face is drawn upright into a hidden canvas once per second, then copied pre-rotated into
 * the visible canvas. An LVGL transform would instead re-rotate the whole face on every frame
 * (well over 100 ms each), which made the screen fade-in effectively skip. */
static lv_obj_t *s_draw_canvas;
static lv_obj_t *s_canvas;
static uint16_t *s_draw_buf;
static uint16_t *s_buf;
static lv_timer_t *s_timer;

/* Two-pass glow: a few dim offset copies fake a soft halo, then a crisp pass on top.
 * Left-aligned starting at x1 (not centered) so callers can place adjacent text blocks
 * (e.g. seconds right after HH:MM) using a measured width instead of guessing. */
static void draw_glow_text(lv_layer_t *layer, const char *text, const lv_font_t *font,
                           int32_t x1, int32_t cy, int32_t width, lv_color_t color)
{
    int32_t lh = lv_font_get_line_height(font);
    lv_area_t area = { x1, cy - lh / 2, x1 + width, cy + lh / 2 };

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = font;
    dsc.align = LV_TEXT_ALIGN_LEFT;

    static const int32_t glow_offs[][2] = { { -3, 0 }, { 3, 0 }, { 0, -3 }, { 0, 3 },
                                             { -3, -3 }, { 3, 3 }, { -3, 3 }, { 3, -3 } };
    dsc.color = lv_color_hex(COLOR_PRIMARY);
    dsc.opa = LV_OPA_50;
    for (size_t i = 0; i < sizeof(glow_offs) / sizeof(glow_offs[0]); i++) {
        lv_area_t a = { area.x1 + glow_offs[i][0], area.y1 + glow_offs[i][1],
                        area.x2 + glow_offs[i][0], area.y2 + glow_offs[i][1] };
        lv_draw_label(layer, &dsc, &a);
    }

    dsc.color = color;
    dsc.opa = LV_OPA_COVER;
    lv_draw_label(layer, &dsc, &area);
}

static void draw_plain_text(lv_layer_t *layer, const char *text, const lv_font_t *font,
                            int32_t cx, int32_t cy, int32_t box_half_w, lv_color_t color)
{
    int32_t lh = lv_font_get_line_height(font);
    lv_area_t area = { cx - box_half_w, cy - lh / 2, cx + box_half_w, cy + lh / 2 };

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = font;
    dsc.align = LV_TEXT_ALIGN_CENTER;
    dsc.color = color;
    dsc.opa = LV_OPA_COVER;
    lv_draw_label(layer, &dsc, &area);
}

static void to_upper(char *s)
{
    for (; *s; s++) {
        *s = (char)toupper((unsigned char)*s);
    }
}

/* Same mapping as an LVGL transform_rotation of UI_ROTATION_DEG (clockwise, about the center),
 * walked in tiles so both the PSRAM reads and writes stay cache-friendly. */
static void rotate_into(uint16_t *dst, const uint16_t *src)
{
    const int32_t n = FACE_SIZE;
    const int32_t tile = 16;
    for (int32_t ty = 0; ty < n; ty += tile) {
        for (int32_t tx = 0; tx < n; tx += tile) {
            int32_t ye = LV_MIN(ty + tile, n);
            int32_t xe = LV_MIN(tx + tile, n);
            for (int32_t ly = ty; ly < ye; ly++) {
                for (int32_t lx = tx; lx < xe; lx++) {
                    int32_t px;
                    int32_t py;
                    switch (UI_ROTATION_DEG) {
                    case 90: px = n - 1 - ly; py = lx; break;
                    case 180: px = n - 1 - lx; py = n - 1 - ly; break;
                    case 270: px = ly; py = n - 1 - lx; break;
                    default: px = lx; py = ly; break;
                    }
                    dst[py * n + px] = src[ly * n + lx];
                }
            }
        }
    }
}

static void redraw_clock(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char time_buf[8];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    char sec_buf[4];
    snprintf(sec_buf, sizeof(sec_buf), "%02d", tm_now.tm_sec);

    char date_buf[16];
    /* Not yet synced (no RTC/SNTP fix) looks like the epoch; say so instead of a bogus date. */
    if (tm_now.tm_year + 1900 < 2020) {
        snprintf(date_buf, sizeof(date_buf), "syncing...");
    } else {
        strftime(date_buf, sizeof(date_buf), "%a, %b %d", &tm_now);
        to_upper(date_buf);
    }

    lv_canvas_fill_bg(s_draw_canvas, lv_color_black(), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(s_draw_canvas, &layer);

    const lv_image_dsc_t *bg = images_get(IMAGE_CLOCK_BACKGROUND);
    if (bg) {
        lv_draw_image_dsc_t img_dsc;
        lv_draw_image_dsc_init(&img_dsc);
        img_dsc.src = bg;
        lv_area_t img_area = { 0, 0, FACE_SIZE - 1, FACE_SIZE - 1 };
        lv_draw_image(&layer, &img_dsc, &img_area);
    }

    draw_plain_text(&layer, date_buf, &fredoka_bold_40, FACE_CX, FACE_CY - 135, 170,
                    lv_color_hex(COLOR_SECONDARY));

    /* Seconds sit right after HH:MM on the same line (measured, not guessed, so the gap
     * stays correct regardless of which digits are showing). */
    lv_point_t hm_size;
    lv_point_t sec_size;
    lv_text_get_size(&hm_size, time_buf, &fredoka_bold_144, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&sec_size, sec_buf, &fredoka_bold_40, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int32_t gap = 8;
    int32_t total_w = hm_size.x + gap + sec_size.x;
    int32_t left_x = FACE_CX - total_w / 2;

    /* Match font baselines rather than vertically centering the smaller seconds. */
    int32_t hm_height = lv_font_get_line_height(&fredoka_bold_144);
    int32_t sec_height = lv_font_get_line_height(&fredoka_bold_40);
    int32_t baseline = FACE_CY - hm_height / 2 + hm_height - fredoka_bold_144.base_line;
    int32_t sec_cy = baseline - sec_height + fredoka_bold_40.base_line + sec_height / 2;

    draw_glow_text(&layer, time_buf, &fredoka_bold_144, left_x, FACE_CY, hm_size.x,
                  lv_color_hex(COLOR_TEXT));
    draw_glow_text(&layer, sec_buf, &fredoka_bold_40, left_x + hm_size.x + gap, sec_cy, sec_size.x,
                  lv_color_hex(COLOR_SECONDARY));

    lv_canvas_finish_layer(s_draw_canvas, &layer);

    rotate_into(s_buf, s_draw_buf);
    lv_obj_invalidate(s_canvas);
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    redraw_clock();
}

static void clock_screen_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    if (s_buf) {
        heap_caps_free(s_buf);
        s_buf = NULL;
    }
    if (s_draw_buf) {
        heap_caps_free(s_draw_buf);
        s_draw_buf = NULL;
    }
    s_canvas = NULL;
    s_draw_canvas = NULL;
}

const uint16_t *ui_screen_clock_panel_pixels(void)
{
    return s_buf;
}

lv_obj_t *ui_screen_clock_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    size_t buf_size = FACE_SIZE * FACE_SIZE * lv_color_format_get_size(LV_COLOR_FORMAT_RGB565);
    /* PSRAM: internal RAM is a scarce pool shared with the SPI driver's DMA descriptors. */
    s_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_draw_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buf) {
        s_buf = heap_caps_malloc(buf_size, MALLOC_CAP_8BIT);
    }
    if (!s_draw_buf) {
        s_draw_buf = heap_caps_malloc(buf_size, MALLOC_CAP_8BIT);
    }

    /* Opaque RGB565 face over the black screen: the cheapest thing for LVGL to blit and fade. */
    s_draw_canvas = lv_canvas_create(scr);
    lv_obj_set_hidden(s_draw_canvas, true);
    lv_canvas_set_buffer(s_draw_canvas, s_draw_buf, FACE_SIZE, FACE_SIZE, LV_COLOR_FORMAT_RGB565);

    s_canvas = lv_canvas_create(scr);
    lv_obj_remove_style_all(s_canvas);
    lv_canvas_set_buffer(s_canvas, s_buf, FACE_SIZE, FACE_SIZE, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(s_canvas);

    redraw_clock();
    s_timer = lv_timer_create(clock_timer_cb, CLOCK_UPDATE_MS, NULL);
    lv_obj_add_event_cb(scr, clock_screen_delete_cb, LV_EVENT_DELETE, NULL);

    return scr;
}
