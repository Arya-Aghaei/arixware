#include "ui_screen_quote.h"
#include "ui_draw.h"
#include "fonts.h"
#include "audio.h"
#include "mmap_generate_sounds.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>

#define CHECK_MS 1000
#define AUTO_REFRESH_MS 60000 /* long enough to read the longest quote twice */
#define FADE_MS 450
#define TEXT_W 300
#define TEXT_TOP 66
#define TEXT_MAX_H 262
#define AUTHOR_Y 362
#define BUTTON_X UI_FACE_CX
#define BUTTON_Y 410
#define BUTTON_R 24
#define STAR_COUNT 70

typedef struct {
    const char *quote;
    const char *author; /* NULL when unknown */
} quote_t;

static const quote_t s_quotes[] = {
#include "quotes_data.inc"
};
#define QUOTE_COUNT ((int)(sizeof(s_quotes) / sizeof(s_quotes[0])))

/* Shuffled deck: every quote shows once before any repeats. */
static uint8_t s_deck[QUOTE_COUNT];
static int s_deck_pos = QUOTE_COUNT;
static int s_current = -1;

static ui_face_t s_face;
static lv_timer_t *s_timer;
static uint32_t s_shown_at;

static int next_quote(void)
{
    if (s_deck_pos >= QUOTE_COUNT) {
        for (int i = 0; i < QUOTE_COUNT; i++) {
            s_deck[i] = (uint8_t)i;
        }
        for (int i = QUOTE_COUNT - 1; i > 0; i--) {
            int j = (int)(esp_random() % (uint32_t)(i + 1));
            uint8_t t = s_deck[i];
            s_deck[i] = s_deck[j];
            s_deck[j] = t;
        }
        if (s_deck[0] == s_current) {
            s_deck[0] = s_deck[1];
            s_deck[1] = (uint8_t)s_current;
        }
        s_deck_pos = 0;
    }
    return s_deck[s_deck_pos++];
}

/* Largest font whose wrapped text fits the circle. */
static const lv_font_t *pick_font(const char *text, int32_t *height)
{
    static const lv_font_t *fonts[] = { &fredoka_medium_28, &fredoka_medium_26, &fredoka_medium_24,
                                        &fredoka_medium_22, &fredoka_medium_16 };
    const lv_font_t *font = fonts[0];
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        font = fonts[i];
        lv_point_t size;
        lv_text_get_size(&size, text, font, 0, 0, TEXT_W, LV_TEXT_FLAG_NONE);
        *height = size.y;
        if (size.y <= TEXT_MAX_H) {
            break;
        }
    }
    return font;
}

/* A quiet starfield that changes with each quote, plus a soft glow behind the text. */
static void draw_backdrop(lv_layer_t *layer, uint32_t seed)
{
    /* Warm dim colour: a faint orange accumulates unevenly across RGB565 channels and turns green. */
    ui_draw_halo(layer, UI_FACE_CX, 210, 60, 190, 0x6A3C16, 150);
    for (int i = 0; i < STAR_COUNT; i++) {
        seed = seed * 1664525u + 1013904223u;
        int x = (int)((seed >> 8) % UI_FACE_SIZE);
        seed = seed * 1664525u + 1013904223u;
        int y = (int)((seed >> 8) % UI_FACE_SIZE);
        seed = seed * 1664525u + 1013904223u;
        int r = (seed >> 24) % 5 == 0 ? 2 : 1;
        lv_opa_t opa = (lv_opa_t)(50 + (seed >> 16) % 150);
        ui_draw_disc(layer, x, y, r, (seed >> 12) % 3 ? UI_COLOR_TEXT : UI_COLOR_PRIMARY, opa);
    }
    ui_draw_ring(layer, UI_FACE_CX, UI_FACE_CY, 214, 3, UI_COLOR_DIM, LV_OPA_50);
}

static void draw_refresh_button(lv_layer_t *layer)
{
    ui_draw_halo(layer, BUTTON_X, BUTTON_Y, BUTTON_R, BUTTON_R + 16, UI_COLOR_PRIMARY, 80);
    ui_draw_disc(layer, BUTTON_X, BUTTON_Y, BUTTON_R, 0x1C1208, LV_OPA_COVER);
    ui_draw_ring(layer, BUTTON_X, BUTTON_Y, BUTTON_R, 3, UI_COLOR_PRIMARY, LV_OPA_COVER);
    /* Circular arrow: an open arc with a head at its clockwise end. */
    ui_draw_arc(layer, BUTTON_X, BUTTON_Y, 11, 4, 20, 330, UI_COLOR_TEXT, LV_OPA_COVER);
    ui_draw_triangle(layer, BUTTON_X + 14, BUTTON_Y + 1, BUTTON_X + 5, BUTTON_Y - 9,
                     BUTTON_X + 15, BUTTON_Y - 10, UI_COLOR_TEXT);
}

static void draw_quote(lv_layer_t *layer, const quote_t *q, uint32_t seed)
{
    draw_backdrop(layer, seed);

    int32_t height;
    const lv_font_t *font = pick_font(q->quote, &height);
    int32_t top = TEXT_TOP + (TEXT_MAX_H - height) / 2;
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = q->quote;
    dsc.font = font;
    dsc.align = LV_TEXT_ALIGN_CENTER;
    dsc.color = lv_color_hex(UI_COLOR_TEXT);
    dsc.opa = LV_OPA_COVER;
    lv_area_t area = { UI_FACE_CX - TEXT_W / 2, top, UI_FACE_CX + TEXT_W / 2, top + height };
    lv_draw_label(layer, &dsc, &area);

    char author[64];
    snprintf(author, sizeof(author), "- %s", q->author ? q->author : "Anonymous");
    ui_draw_text(layer, author, &fredoka_medium_22, UI_FACE_CX, AUTHOR_Y,
                 q->author ? UI_COLOR_PRIMARY : UI_COLOR_DIM, LV_OPA_COVER);
    draw_refresh_button(layer);
}

static void fade_done(void *ctx)
{
    (void)ctx;
    /* The panel now shows the new frame; make the current one match so LVGL and later sends agree. */
    if (s_face.buf && s_face.alt_buf) {
        memcpy(s_face.buf, s_face.alt_buf, UI_FACE_SIZE * UI_FACE_SIZE * sizeof(uint16_t));
    }
}

static void show_next(bool animate)
{
    if (ui_panel_fade_active()) {
        return;
    }
    s_current = next_quote();
    s_shown_at = lv_tick_get();
    lv_layer_t layer;
    ui_face_begin_plain(&s_face, &layer);
    draw_quote(&layer, &s_quotes[s_current], (uint32_t)s_current * 2654435761u + 12345u);
    if (animate && s_face.direct && ui_face_end_peak(&s_face, &layer) &&
        ui_panel_fade(s_face.buf, s_face.alt_buf, FADE_MS, fade_done, NULL)) {
        return;
    }
    ui_face_end(&s_face, &layer);
}

static void click_cb(lv_event_t *e)
{
    (void)e;
    int x, y;
    ui_face_pointer(&x, &y);
    int dx = x - BUTTON_X;
    int dy = y - BUTTON_Y;
    int reach = BUTTON_R + 16;
    if (dx * dx + dy * dy > reach * reach || ui_panel_fade_active()) {
        return;
    }
    audio_play_index(MMAP_SOUNDS_TOUCH_WAV);
    show_next(true);
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (lv_tick_elaps(s_shown_at) >= AUTO_REFRESH_MS) {
        show_next(true);
    }
}

static void delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    ui_face_destroy(&s_face);
}

const uint16_t *ui_screen_quote_panel_pixels(void)
{
    return s_face.buf;
}

lv_obj_t *ui_screen_quote_create(void)
{
    lv_obj_t *scr = ui_face_create(&s_face);
    if (!scr) {
        return lv_obj_create(NULL);
    }
    /* Reopening keeps the quote on screen; a fresh one only when none was ever shown. */
    bool fresh = s_current < 0 || lv_tick_elaps(s_shown_at) >= AUTO_REFRESH_MS;
    if (fresh) {
        s_current = next_quote();
        s_shown_at = lv_tick_get();
    }
    lv_layer_t layer;
    ui_face_begin_plain(&s_face, &layer);
    draw_quote(&layer, &s_quotes[s_current], (uint32_t)s_current * 2654435761u + 12345u);
    ui_face_end(&s_face, &layer);
    s_timer = lv_timer_create(timer_cb, CHECK_MS, NULL);
    lv_obj_add_event_cb(scr, click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, delete_cb, LV_EVENT_DELETE, NULL);
    return scr;
}
