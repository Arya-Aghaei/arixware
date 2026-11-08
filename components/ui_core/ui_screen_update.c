#include "ui_screen_update.h"
#include "ui_draw.h"
#include "fonts.h"
#include <math.h>
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

/* One glossy eye orb as on the eyes screen: dark rim with an orange glow, cream-to-amber
 * gradient, a halftone dot band along the bottom and a slanted highlight. */
static void draw_orb(lv_layer_t *layer, int cx, int cy, int r, bool peak, bool dim)
{
    ui_draw_halo(layer, cx, cy, r, r + (peak ? 26 : 18), UI_COLOR_PRIMARY, dim ? 40 : (peak ? 150 : 100));
    ui_draw_disc(layer, cx, cy, r + 2, 0x5A2A0A, LV_OPA_COVER);
    ui_draw_ring(layer, cx, cy, r + 3, 2, UI_COLOR_PRIMARY, LV_OPA_COVER);

    int in = r - 7;
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.radius = LV_RADIUS_CIRCLE;
    rect.bg_opa = LV_OPA_COVER;
    rect.bg_grad.dir = LV_GRAD_DIR_VER;
    rect.bg_grad.stops_count = 2;
    rect.bg_grad.stops[0].color = lv_color_hex(dim ? 0xB89A6A : 0xFFF1CF);
    rect.bg_grad.stops[0].opa = LV_OPA_COVER;
    rect.bg_grad.stops[0].frac = 0;
    rect.bg_grad.stops[1].color = lv_color_hex(dim ? 0x9A5A1A : 0xFF9A14);
    rect.bg_grad.stops[1].opa = LV_OPA_COVER;
    rect.bg_grad.stops[1].frac = 255;
    lv_area_t area = { cx - in, cy - in, cx + in, cy + in };
    lv_draw_rect(layer, &rect, &area);

    /* Halftone: dots grow towards the bottom edge, clipped to the orb. */
    for (int row = 0; row < 5; row++) {
        int y = cy + in * 2 / 10 + row * 9;
        int dot = 1 + row * 4 / 5 + (row > 2 ? 1 : 0);
        for (int x = cx - in; x <= cx + in; x += 11) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= (in - 3 - dot) * (in - 3 - dot)) {
                ui_draw_disc(layer, x + (row & 1) * 5, y, dot, 0xFFC94A, LV_OPA_70);
            }
        }
    }
    ui_draw_arc(layer, cx, cy, in * 3 / 4, 7, 215, 250, 0xFFFFFF, LV_OPA_70);
    ui_draw_disc(layer, cx - in * 3 / 5, cy - in * 2 / 5 + 2, 4, 0xFFFFFF, LV_OPA_70);
}

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

    draw_orb(layer, UI_FACE_CX - ORB_DX, ORB_Y, ORB_R, peak, failed);
    draw_orb(layer, UI_FACE_CX + ORB_DX, ORB_Y, ORB_R, peak, failed);

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
