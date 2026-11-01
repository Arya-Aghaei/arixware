#include "ui_screen_alarm.h"
#include "ui_tools.h"
#include "ui_draw.h"
#include "fonts.h"
#include "audio.h"
#include "mmap_generate_sounds.h"
#include <stdio.h>

#define REFRESH_MS 250
#define TIME_Y 200
#define HOUR_X (UI_FACE_CX - 58)
#define MIN_X (UI_FACE_CX + 58)
#define UP_Y 140
#define DOWN_Y 262
#define BUTTON_Y 338
#define PILL_HALF_H 30
#define DARK 0x1C1208

typedef enum { HIT_NONE, HIT_HOUR_UP, HIT_HOUR_DOWN, HIT_MIN_UP, HIT_MIN_DOWN, HIT_TOGGLE,
               HIT_SNOOZE, HIT_WAKE } hit_t;

static ui_face_t s_face;
static lv_timer_t *s_timer;
static uint32_t s_drawn_key = UINT32_MAX;

static void draw_arrow(lv_layer_t *layer, int cx, int cy, bool up, uint32_t color)
{
    int tip = up ? cy - 11 : cy + 11;
    int base = up ? cy + 11 : cy - 11;
    ui_draw_triangle(layer, cx, tip, cx - 20, base, cx + 20, base, color);
}

static void format_countdown(char *out, size_t size, int seconds)
{
    int minutes = (seconds + 59) / 60;
    if (minutes >= 60) {
        snprintf(out, size, "rings in %dh %dm", minutes / 60, minutes % 60);
    } else {
        snprintf(out, size, "rings in %dm", minutes < 1 ? 1 : minutes);
    }
}

static void draw_face(lv_layer_t *layer, bool peak)
{
    ui_alarm_state_t state = ui_alarm_state();
    int hour = ui_alarm_hour();
    int minute = ui_alarm_minute();
    int until = ui_alarm_seconds_until();
    bool ringing = state == UI_ALARM_RINGING;
    bool armed = state == UI_ALARM_ARMED || state == UI_ALARM_SNOOZED;
    uint32_t time_color = armed || ringing ? UI_COLOR_TEXT : UI_COLOR_SECONDARY;

    if (ringing) {
        ui_draw_halo(layer, UI_FACE_CX, TIME_Y, 80, peak ? 210 : 150, UI_COLOR_PRIMARY, peak ? 180 : 70);
    }
    ui_draw_text(layer, ringing ? "WAKE UP!" : state == UI_ALARM_SNOOZED ? "SNOOZING" : "ALARM",
                 &fredoka_medium_26, UI_FACE_CX, 100, ringing ? UI_COLOR_TEXT : UI_COLOR_SECONDARY, LV_OPA_COVER);

    char text[4];
    snprintf(text, sizeof(text), "%02d", hour);
    ui_draw_glow_text(layer, text, &fredoka_bold_72, HOUR_X, TIME_Y, time_color);
    snprintf(text, sizeof(text), "%02d", minute);
    ui_draw_glow_text(layer, text, &fredoka_bold_72, MIN_X, TIME_Y, time_color);
    ui_draw_glow_text(layer, ":", &fredoka_bold_72, UI_FACE_CX, TIME_Y - 4, time_color);

    if (ringing) {
        ui_draw_pill(layer, 88, BUTTON_Y - PILL_HALF_H, 226, BUTTON_Y + PILL_HALF_H, DARK, LV_OPA_COVER, UI_COLOR_PRIMARY, 3);
        ui_draw_text(layer, "SNOOZE", &fredoka_medium_26, 157, BUTTON_Y, UI_COLOR_TEXT, LV_OPA_COVER);
        ui_draw_halo(layer, 309, BUTTON_Y, 60, 85, UI_COLOR_PRIMARY, 110);
        ui_draw_pill(layer, 240, BUTTON_Y - PILL_HALF_H, 378, BUTTON_Y + PILL_HALF_H, UI_COLOR_PRIMARY, LV_OPA_COVER, UI_COLOR_TEXT, 3);
        ui_draw_text(layer, "WAKE UP", &fredoka_medium_26, 309, BUTTON_Y, DARK, LV_OPA_COVER);
        ui_draw_text(layer, "snooze adds 9 minutes", &fredoka_medium_16, UI_FACE_CX, 396, UI_COLOR_SECONDARY, LV_OPA_COVER);
    } else {
        uint32_t arrow = UI_COLOR_PRIMARY;
        draw_arrow(layer, HOUR_X, UP_Y, true, arrow);
        draw_arrow(layer, HOUR_X, DOWN_Y, false, arrow);
        draw_arrow(layer, MIN_X, UP_Y, true, arrow);
        draw_arrow(layer, MIN_X, DOWN_Y, false, arrow);

        if (armed) {
            ui_draw_halo(layer, UI_FACE_CX, BUTTON_Y, 50, 92, UI_COLOR_PRIMARY, 110);
            ui_draw_pill(layer, UI_FACE_CX - 80, BUTTON_Y - PILL_HALF_H, UI_FACE_CX + 80, BUTTON_Y + PILL_HALF_H,
                         UI_COLOR_PRIMARY, LV_OPA_COVER, UI_COLOR_TEXT, 3);
            ui_draw_text(layer, "ALARM ON", &fredoka_medium_26, UI_FACE_CX, BUTTON_Y, DARK, LV_OPA_COVER);
            char caption[24];
            format_countdown(caption, sizeof(caption), until < 0 ? 0 : until);
            ui_draw_text(layer, until < 0 ? "waiting for clock sync" : caption, &fredoka_medium_22,
                         UI_FACE_CX, 396, UI_COLOR_SECONDARY, LV_OPA_COVER);
        } else {
            ui_draw_pill(layer, UI_FACE_CX - 80, BUTTON_Y - PILL_HALF_H, UI_FACE_CX + 80, BUTTON_Y + PILL_HALF_H,
                         DARK, LV_OPA_COVER, UI_COLOR_DIM, 3);
            ui_draw_text(layer, "ALARM OFF", &fredoka_medium_26, UI_FACE_CX, BUTTON_Y, UI_COLOR_SECONDARY, LV_OPA_COVER);
            ui_draw_text(layer, "tap to arm", &fredoka_medium_22, UI_FACE_CX, 396, UI_COLOR_DIM, LV_OPA_COVER);
        }
    }

}

static void redraw(void)
{
    ui_alarm_state_t state = ui_alarm_state();
    int until = ui_alarm_seconds_until();
    uint32_t countdown_min = until < 0 ? 0 : (uint32_t)(until + 59) / 60;
    uint32_t key = (uint32_t)state | (uint32_t)ui_alarm_hour() << 5 | (uint32_t)ui_alarm_minute() << 10 |
                   countdown_min << 16;
    if (key == s_drawn_key) {
        return;
    }
    s_drawn_key = key;

    lv_layer_t layer;
    ui_face_begin(&s_face, &layer, IMAGE_ALARM_BACKGROUND);
    draw_face(&layer, false);
    ui_face_end(&s_face, &layer);

    /* Ringing breathes between this frame and a brighter one, blended straight on the panel. */
    if (state == UI_ALARM_RINGING) {
        ui_face_begin(&s_face, &layer, IMAGE_ALARM_BACKGROUND);
        draw_face(&layer, true);
        ui_face_set_pulse(&s_face, ui_face_end_peak(&s_face, &layer));
    } else {
        ui_face_set_pulse(&s_face, false);
    }
}

static bool in_box(int x, int y, int cx, int cy, int half_w, int half_h)
{
    return x >= cx - half_w && x <= cx + half_w && y >= cy - half_h && y <= cy + half_h;
}

static hit_t hit_test(int x, int y)
{
    if (ui_alarm_state() == UI_ALARM_RINGING) {
        if (in_box(x, y, 157, BUTTON_Y, 74, PILL_HALF_H + 8)) {
            return HIT_SNOOZE;
        }
        if (in_box(x, y, 309, BUTTON_Y, 74, PILL_HALF_H + 8)) {
            return HIT_WAKE;
        }
        return HIT_NONE;
    }
    if (in_box(x, y, HOUR_X, UP_Y, 46, 34)) return HIT_HOUR_UP;
    if (in_box(x, y, HOUR_X, DOWN_Y, 46, 34)) return HIT_HOUR_DOWN;
    if (in_box(x, y, MIN_X, UP_Y, 46, 34)) return HIT_MIN_UP;
    if (in_box(x, y, MIN_X, DOWN_Y, 46, 34)) return HIT_MIN_DOWN;
    if (in_box(x, y, UI_FACE_CX, BUTTON_Y, 88, PILL_HALF_H + 8)) return HIT_TOGGLE;
    return HIT_NONE;
}

static void apply_hit(hit_t hit, bool repeat)
{
    switch (hit) {
    case HIT_HOUR_UP: ui_alarm_adjust(1, 0); break;
    case HIT_HOUR_DOWN: ui_alarm_adjust(-1, 0); break;
    case HIT_MIN_UP: ui_alarm_adjust(0, 1); break;
    case HIT_MIN_DOWN: ui_alarm_adjust(0, -1); break;
    case HIT_TOGGLE:
        if (repeat) return;
        ui_alarm_set_enabled(ui_alarm_state() == UI_ALARM_OFF);
        break;
    case HIT_SNOOZE:
        if (repeat) return;
        ui_alarm_snooze();
        break;
    case HIT_WAKE:
        if (repeat) return;
        ui_alarm_wake_up();
        break;
    case HIT_NONE:
        return;
    }
    if (!repeat) {
        audio_play_index(hit == HIT_WAKE ? MMAP_SOUNDS_SUCCESS_WAV : MMAP_SOUNDS_TOUCH_WAV);
    }
    s_drawn_key = UINT32_MAX;
    redraw();
}

static void input_cb(lv_event_t *e)
{
    int x, y;
    ui_face_pointer(&x, &y);
    /* Holding an arrow keeps stepping; buttons only act on a plain tap. */
    apply_hit(hit_test(x, y), lv_event_get_code(e) == LV_EVENT_LONG_PRESSED_REPEAT);
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

const uint16_t *ui_screen_alarm_panel_pixels(void)
{
    return s_face.buf;
}

lv_obj_t *ui_screen_alarm_create(void)
{
    lv_obj_t *scr = ui_face_create(&s_face);
    if (!scr) {
        return lv_obj_create(NULL);
    }
    s_drawn_key = UINT32_MAX;
    redraw();
    s_timer = lv_timer_create(timer_cb, REFRESH_MS, NULL);
    lv_obj_add_event_cb(scr, input_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, input_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
    lv_obj_add_event_cb(scr, delete_cb, LV_EVENT_DELETE, NULL);
    return scr;
}
