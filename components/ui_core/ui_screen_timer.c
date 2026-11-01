#include "ui_screen_timer.h"
#include "ui_tools.h"
#include "ui_draw.h"
#include "fonts.h"
#include "audio.h"
#include "mmap_generate_sounds.h"
#include <stdio.h>

#define REFRESH_MS 250
#define RING_RADIUS 214
#define RING_WIDTH 10
#define BUTTON_Y 338
#define SIDE_R 34
#define MAIN_R 46
#define SIDE_DX 92

typedef enum { BTN_NONE, BTN_LEFT, BTN_MAIN, BTN_RIGHT } button_t;

static ui_face_t s_face;
static lv_timer_t *s_timer;
static uint32_t s_drawn_key = UINT32_MAX;

static void draw_line(lv_layer_t *layer, int x1, int y1, int x2, int y2, int width, uint32_t color)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1.x = x1; dsc.p1.y = y1;
    dsc.p2.x = x2; dsc.p2.y = y2;
    dsc.width = width;
    dsc.color = lv_color_hex(color);
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = dsc.round_end = 1;
    lv_draw_line(layer, &dsc);
}

static void draw_button(lv_layer_t *layer, int cx, int r, bool filled)
{
    ui_draw_halo(layer, cx, BUTTON_Y, r, r + 18, UI_COLOR_PRIMARY, filled ? 110 : 60);
    ui_draw_disc(layer, cx, BUTTON_Y, r, filled ? UI_COLOR_PRIMARY : 0x1C1208, LV_OPA_COVER);
    ui_draw_ring(layer, cx, BUTTON_Y, r, 3, filled ? UI_COLOR_TEXT : UI_COLOR_PRIMARY, LV_OPA_COVER);
}

static void draw_plus_minus(lv_layer_t *layer, int cx, bool plus)
{
    ui_draw_pill(layer, cx - 14, BUTTON_Y - 3, cx + 14, BUTTON_Y + 3, UI_COLOR_TEXT, LV_OPA_COVER, UI_COLOR_TEXT, 0);
    if (plus) {
        ui_draw_pill(layer, cx - 3, BUTTON_Y - 14, cx + 3, BUTTON_Y + 14, UI_COLOR_TEXT, LV_OPA_COVER, UI_COLOR_TEXT, 0);
    }
}

static void draw_face(lv_layer_t *layer, bool peak)
{
    ui_timer_state_t state = ui_timer_state();
    uint32_t left_ms = ui_timer_remaining_ms();
    uint32_t total_ms = ui_timer_total_ms();
    uint32_t left_s = (left_ms + 999) / 1000;
    bool ringing = state == UI_TIMER_RINGING;
    bool active = state == UI_TIMER_RUNNING || state == UI_TIMER_PAUSED;

    /* Progress ring around the bezel: dim track plus the time still left. */
    ui_draw_ring(layer, UI_FACE_CX, UI_FACE_CY, RING_RADIUS, RING_WIDTH, UI_COLOR_DIM, LV_OPA_40);
    if (ringing) {
        ui_draw_ring(layer, UI_FACE_CX, UI_FACE_CY, RING_RADIUS, RING_WIDTH + (peak ? 8 : 0), UI_COLOR_PRIMARY, LV_OPA_COVER);
    } else if (total_ms && left_ms) {
        uint32_t sweep = (uint32_t)((uint64_t)360 * left_ms / total_ms);
        if (sweep >= 360 || !active) {
            ui_draw_ring(layer, UI_FACE_CX, UI_FACE_CY, RING_RADIUS, RING_WIDTH, UI_COLOR_PRIMARY, LV_OPA_COVER);
        } else if (sweep > 0) {
            ui_draw_arc(layer, UI_FACE_CX, UI_FACE_CY, RING_RADIUS, RING_WIDTH, 270,
                        (270 + (int)sweep) % 360, UI_COLOR_PRIMARY, LV_OPA_COVER);
        }
    }

    if (ringing) {
        ui_draw_halo(layer, UI_FACE_CX, 205, 90, peak ? 200 : 140, UI_COLOR_PRIMARY, peak ? 170 : 60);
    }

    ui_draw_text(layer, "TIMER", &fredoka_medium_26, UI_FACE_CX, 100, UI_COLOR_SECONDARY, LV_OPA_COVER);

    char text[16];
    snprintf(text, sizeof(text), "%02u:%02u", (unsigned)(left_s / 60 % 100), (unsigned)(left_s % 60));
    ui_draw_glow_text(layer, text, &fredoka_bold_72, UI_FACE_CX, 200,
                      state == UI_TIMER_PAUSED ? UI_COLOR_SECONDARY : UI_COLOR_TEXT);

    const char *caption = ringing ? "TIME'S UP" : state == UI_TIMER_PAUSED ? "PAUSED"
                        : state == UI_TIMER_RUNNING ? "COUNTING DOWN" : "SET MINUTES";
    ui_draw_text(layer, caption, &fredoka_medium_26, UI_FACE_CX, 262, ringing ? UI_COLOR_TEXT : UI_COLOR_SECONDARY, LV_OPA_COVER);

    int lx = UI_FACE_CX - SIDE_DX;
    int rx = UI_FACE_CX + SIDE_DX;
    if (ringing) {
        draw_button(layer, UI_FACE_CX, MAIN_R, true);
        draw_line(layer, UI_FACE_CX - 14, BUTTON_Y - 14, UI_FACE_CX + 14, BUTTON_Y + 14, 6, 0x1C1208);
        draw_line(layer, UI_FACE_CX - 14, BUTTON_Y + 14, UI_FACE_CX + 14, BUTTON_Y - 14, 6, 0x1C1208);
    } else {
        draw_button(layer, UI_FACE_CX, MAIN_R, true);
        if (state == UI_TIMER_RUNNING) {
            ui_draw_pill(layer, UI_FACE_CX - 14, BUTTON_Y - 17, UI_FACE_CX - 5, BUTTON_Y + 17, 0x1C1208, LV_OPA_COVER, 0x1C1208, 0);
            ui_draw_pill(layer, UI_FACE_CX + 5, BUTTON_Y - 17, UI_FACE_CX + 14, BUTTON_Y + 17, 0x1C1208, LV_OPA_COVER, 0x1C1208, 0);
        } else {
            ui_draw_triangle(layer, UI_FACE_CX - 10, BUTTON_Y - 18, UI_FACE_CX - 10, BUTTON_Y + 18,
                             UI_FACE_CX + 20, BUTTON_Y, 0x1C1208);
        }
        draw_button(layer, lx, SIDE_R, false);
        if (active) {
            lv_draw_rect_dsc_t stop;
            lv_draw_rect_dsc_init(&stop);
            stop.bg_color = lv_color_hex(UI_COLOR_TEXT);
            stop.radius = 5;
            lv_area_t stop_area = { lx - 11, BUTTON_Y - 11, lx + 11, BUTTON_Y + 11 };
            lv_draw_rect(layer, &stop, &stop_area);
        } else {
            draw_plus_minus(layer, lx, false);
            draw_button(layer, rx, SIDE_R, false);
            draw_plus_minus(layer, rx, true);
        }
    }

}

static void redraw(void)
{
    ui_timer_state_t state = ui_timer_state();
    uint32_t left_s = (ui_timer_remaining_ms() + 999) / 1000;
    uint32_t key = (uint32_t)state << 28 | (ui_timer_total_ms() / 60000) << 16 | left_s;
    if (key == s_drawn_key) {
        return;
    }
    s_drawn_key = key;

    lv_layer_t layer;
    ui_face_begin(&s_face, &layer, IMAGE_TIMER_BACKGROUND);
    draw_face(&layer, false);
    ui_face_end(&s_face, &layer);

    /* Ringing breathes between this frame and a brighter one, blended straight on the panel. */
    if (state == UI_TIMER_RINGING) {
        ui_face_begin(&s_face, &layer, IMAGE_TIMER_BACKGROUND);
        draw_face(&layer, true);
        ui_face_set_pulse(&s_face, ui_face_end_peak(&s_face, &layer));
    } else {
        ui_face_set_pulse(&s_face, false);
    }
}

static button_t hit_test(int x, int y)
{
    int dy = y - BUTTON_Y;
    int dx = x - UI_FACE_CX;
    if (dx * dx + dy * dy <= (MAIN_R + 10) * (MAIN_R + 10)) {
        return BTN_MAIN;
    }
    for (int side = -1; side <= 1; side += 2) {
        int sx = dx - side * SIDE_DX;
        if (sx * sx + dy * dy <= (SIDE_R + 10) * (SIDE_R + 10)) {
            return side < 0 ? BTN_LEFT : BTN_RIGHT;
        }
    }
    return BTN_NONE;
}

static void click_cb(lv_event_t *e)
{
    (void)e;
    int x, y;
    ui_face_pointer(&x, &y);
    button_t btn = hit_test(x, y);
    if (btn == BTN_NONE) {
        return;
    }
    ui_timer_state_t state = ui_timer_state();
    int step = ui_timer_total_ms() / 60000 >= 10 ? 5 : 1;
    if (btn == BTN_MAIN) {
        ui_timer_toggle();
    } else if (state == UI_TIMER_IDLE) {
        ui_timer_adjust(btn == BTN_LEFT ? -step : step);
    } else if (btn == BTN_LEFT) {
        ui_timer_reset();
    } else {
        return;
    }
    audio_play_index(MMAP_SOUNDS_TOUCH_WAV);
    s_drawn_key = UINT32_MAX;
    redraw();
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    redraw();
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

const uint16_t *ui_screen_timer_panel_pixels(void)
{
    return s_face.buf;
}

lv_obj_t *ui_screen_timer_create(void)
{
    lv_obj_t *scr = ui_face_create(&s_face);
    if (!scr) {
        return lv_obj_create(NULL);
    }
    s_drawn_key = UINT32_MAX;
    redraw();
    s_timer = lv_timer_create(timer_cb, REFRESH_MS, NULL);
    lv_obj_add_event_cb(scr, click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, delete_cb, LV_EVENT_DELETE, NULL);
    return scr;
}
