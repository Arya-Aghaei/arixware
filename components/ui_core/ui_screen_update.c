#include "ui_screen_update.h"
#include "ui_draw.h"
#include "fonts.h"
#include <stdio.h>
#include <string.h>

#define RING_RADIUS 214
#define RING_WIDTH 10
#define ORB_R 50
#define ORB_DX 68
#define ORB_Y 118

typedef enum { MODE_INSTALLING, MODE_FAILED, MODE_DONE } mode_t_;

static ui_face_t s_face;
static bool s_alive;
static mode_t_ s_mode;
static int s_percent;
static char s_version[32];
static char s_reason[64];

static void draw_face(lv_layer_t *layer, bool peak)
{
    bool failed = s_mode == MODE_FAILED;

    ui_draw_ring(layer, UI_FACE_CX, UI_FACE_CY, RING_RADIUS, RING_WIDTH, UI_COLOR_DIM, LV_OPA_40);
    int sweep = s_mode == MODE_INSTALLING ? s_percent * 360 / 100 : (s_mode == MODE_DONE ? 360 : 0);
    if (sweep >= 360) {
        ui_draw_ring(layer, UI_FACE_CX, UI_FACE_CY, RING_RADIUS, RING_WIDTH, UI_COLOR_PRIMARY, LV_OPA_COVER);
    } else if (sweep > 0) {
        ui_draw_arc(layer, UI_FACE_CX, UI_FACE_CY, RING_RADIUS, RING_WIDTH, 270, (270 + sweep) % 360,
                    UI_COLOR_PRIMARY, LV_OPA_COVER);
    }

    ui_draw_eye(layer, UI_FACE_CX - ORB_DX, ORB_Y, ORB_R, eyes_themes_get(eyes_themes_selected()), eyes_styles_selected(), peak, failed);
    ui_draw_eye(layer, UI_FACE_CX + ORB_DX, ORB_Y, ORB_R, eyes_themes_get(eyes_themes_selected()), eyes_styles_selected(), peak, failed);

    const char *title = failed ? "UPDATE FAILED" : (s_mode == MODE_DONE ? "UPDATED" : "UPDATING");
    ui_draw_text(layer, title, &fredoka_medium_26, UI_FACE_CX, 208, UI_COLOR_SECONDARY, LV_OPA_COVER);

    if (failed) {
        ui_draw_text(layer, s_reason, &fredoka_medium_26, UI_FACE_CX, 270, UI_COLOR_TEXT, LV_OPA_COVER);
        ui_draw_text(layer, "Your arix keeps working", &fredoka_medium_22, UI_FACE_CX, 330, UI_COLOR_DIM, LV_OPA_COVER);
        return;
    }
    char text[16];
    if (s_mode == MODE_DONE) {
        snprintf(text, sizeof(text), "100");
    } else {
        snprintf(text, sizeof(text), "%d", s_percent);
    }
    ui_draw_glow_text(layer, text, &fredoka_bold_72, UI_FACE_CX, 282, UI_COLOR_TEXT);
    ui_draw_text(layer, s_mode == MODE_DONE ? "RESTARTING" : "DO NOT UNPLUG", &fredoka_medium_26,
                 UI_FACE_CX, 346, UI_COLOR_SECONDARY, LV_OPA_COVER);
    ui_draw_text(layer, s_version, &fredoka_medium_22, UI_FACE_CX, 384, UI_COLOR_DIM, LV_OPA_COVER);
}

static void redraw(void)
{
    if (!s_alive) {
        return;
    }
    lv_layer_t layer;
    ui_face_begin_plain(&s_face, &layer);
    draw_face(&layer, false);
    ui_face_end(&s_face, &layer);
    if (s_mode == MODE_INSTALLING) {
        /* The orbs breathe while installing, like the ringing timer. */
        ui_face_begin_plain(&s_face, &layer);
        draw_face(&layer, true);
        ui_face_set_pulse(&s_face, ui_face_end_peak(&s_face, &layer));
    } else {
        ui_face_set_pulse(&s_face, false);
    }
}

static void delete_cb(lv_event_t *e)
{
    (void)e;
    s_alive = false;
    ui_face_destroy(&s_face);
}

lv_obj_t *ui_screen_update_create(const char *version)
{
    s_mode = MODE_INSTALLING;
    s_percent = 0;
    snprintf(s_version, sizeof(s_version), "%s", version ? version : "");
    lv_obj_t *scr = ui_face_create(&s_face);
    if (!scr) {
        return lv_obj_create(NULL);
    }
    s_alive = true;
    lv_obj_add_event_cb(scr, delete_cb, LV_EVENT_DELETE, NULL);
    redraw();
    return scr;
}

void ui_screen_update_set_progress(int percent)
{
    percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    if (!s_alive || percent == s_percent) {
        return;
    }
    s_percent = percent;
    redraw();
}

void ui_screen_update_set_failed(const char *reason)
{
    s_mode = MODE_FAILED;
    snprintf(s_reason, sizeof(s_reason), "%s", reason ? reason : "");
    redraw();
}

void ui_screen_update_set_done(void)
{
    s_mode = MODE_DONE;
    redraw();
}
