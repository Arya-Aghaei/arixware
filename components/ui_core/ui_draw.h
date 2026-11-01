#pragma once
#include "lvgl.h"
#include "images.h"
#include "ui_panel.h"
#include <stdbool.h>
#include <stdint.h>

/* Shared drawing for the canvas-based widget screens (timer, alarm). Everything is drawn upright
 * into a hidden canvas, then copied pre-rotated into the visible one (see ui_screen_clock.c). */

#define UI_FACE_SIZE 466
#define UI_FACE_CX 233
#define UI_FACE_CY 233

/* "Cute Cosmic Robot" palette, matching the clock. */
#define UI_COLOR_TEXT      0xFFF4DF
#define UI_COLOR_SECONDARY 0xFFD49B
#define UI_COLOR_PRIMARY   0xFF9347
#define UI_COLOR_DIM       0x7A5A3A

/* Like Settings, a face draws straight to the panel once it is on screen (LVGL needs ~145 ms to
 * flush a full frame; a direct send is a fraction of that). Until then, and if the panel cannot be
 * taken, it falls back to invalidating its canvas. */
typedef struct {
    lv_obj_t *draw_canvas;
    lv_obj_t *canvas;
    uint16_t *draw_buf;
    uint16_t *buf;          /* panel-ready (rotated) current frame */
    uint16_t *alt_buf;      /* panel-ready peak frame for the ringing pulse */
    ui_panel_strips_t strips;
    lv_timer_t *pulse;
    bool direct;
    bool pulse_wanted;
} ui_face_t;

/** Builds a black screen holding the face canvases. Returns NULL if buffers cannot be allocated. */
lv_obj_t *ui_face_create(ui_face_t *face);
void ui_face_destroy(ui_face_t *face);
/** Clears the face and draws the optional background; draw into `layer`, then call ui_face_end(). */
void ui_face_begin(ui_face_t *face, lv_layer_t *layer, image_id_t background);
/** Same, on plain black (screens that draw their own backdrop). */
void ui_face_begin_plain(ui_face_t *face, lv_layer_t *layer);
void ui_face_end(ui_face_t *face, lv_layer_t *layer);
/** Finishes the layer into the pulse's peak frame instead; returns false if it cannot be stored. */
bool ui_face_end_peak(ui_face_t *face, lv_layer_t *layer);
/** Smoothly breathes the panel between the current and peak frames (~60 fps, direct mode only). */
void ui_face_set_pulse(ui_face_t *face, bool on);
/** Upright coordinates of the current pointer position (undoes panel rotation). */
void ui_face_pointer(int *x, int *y);

void ui_draw_text(lv_layer_t *layer, const char *text, const lv_font_t *font,
                  int cx, int cy, uint32_t color, lv_opa_t opa);
/** Text with the clock's soft amber halo. */
void ui_draw_glow_text(lv_layer_t *layer, const char *text, const lv_font_t *font,
                       int cx, int cy, uint32_t color);
void ui_draw_disc(lv_layer_t *layer, int cx, int cy, int r, uint32_t color, lv_opa_t opa);
void ui_draw_ring(lv_layer_t *layer, int cx, int cy, int r, int width, uint32_t color, lv_opa_t opa);
/** Soft glow: stacked translucent discs fading from `inner` to `outer` radius. */
void ui_draw_halo(lv_layer_t *layer, int cx, int cy, int inner, int outer, uint32_t color, lv_opa_t peak);
void ui_draw_arc(lv_layer_t *layer, int cx, int cy, int r, int width, int start_deg, int end_deg,
                 uint32_t color, lv_opa_t opa);
void ui_draw_pill(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t fill, lv_opa_t fill_opa,
                  uint32_t border, int border_width);
void ui_draw_triangle(lv_layer_t *layer, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color);
