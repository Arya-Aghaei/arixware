#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Helpers for screens that draw straight to the panel (see eyes_panel.h). Images are native
 * RGB565, 466x466, already in panel orientation; NULL means black. */

#define UI_PANEL_SIZE 466

typedef struct {
    uint16_t *strip[2];
    size_t pixels;
    int next;
} ui_panel_strips_t;

/** Allocates two DMA-capable strip buffers of `pixels` pixels each. */
bool ui_panel_strips_init(ui_panel_strips_t *strips, size_t pixels);
void ui_panel_strips_free(ui_panel_strips_t *strips);

/** Sends the panel area [x0,x1)x[y0,y1) of `image`, widened to even bounds, and waits for it. */
void ui_panel_send(ui_panel_strips_t *strips, const uint16_t *image, int x0, int y0, int x1, int y1);

/** True while a cross-fade owns the panel; other direct drawers must pause meanwhile. */
bool ui_panel_fade_active(void);

/** Sends one frame blending `from` toward `to` (alpha32 0..32) over the visible circle only. */
void ui_panel_blend(ui_panel_strips_t *strips, const uint16_t *from, const uint16_t *to, uint32_t alpha32);

/**
 * Cross-fades the whole panel from `from` to `to` while drawing directly, then calls done(ctx)
 * from an LVGL timer while it still owns the panel: done must load the screen that matches `to`
 * synchronously, so LVGL never shows an intermediate frame when the panel is handed back.
 * Returns false (and never calls done) if direct drawing is unavailable or a fade is running.
 */
bool ui_panel_fade(const uint16_t *from, const uint16_t *to, uint32_t duration_ms,
                   void (*done)(void *ctx), void *ctx);
