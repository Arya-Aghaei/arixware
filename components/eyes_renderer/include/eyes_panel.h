#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Direct panel access shared by the eyes renderer and other full-screen effects (device: LVGL
 * adapter "dummy draw" mode; host: an in-memory framebuffer). Coordinates are panel pixels,
 * end-exclusive. All calls must come from the LVGL task. */

/* begin/end nest: LVGL gets the panel back (and redraws the active screen) only when the last
 * owner calls end, so load the screen that matches the panel before releasing it. */
bool eyes_panel_begin(void);
void eyes_panel_end(void);
void *eyes_panel_alloc_dma(size_t size);
void eyes_panel_free_dma(void *buf);
/* Pixels are RGB565 byte-swapped, stride = x1 - x0. With wait=false the buffer must stay
 * untouched until the next blit returns (the driver drains queued pixel DMA before it). */
bool eyes_panel_blit(int x0, int y0, int x1, int y1, const void *buf, bool wait);
/* Starts fn(arg) on a helper task (the other CPU core on device) and returns at once; one job at a
 * time. eyes_panel_join() blocks until it has finished (no-op if none is running). */
void eyes_panel_run_async(void (*fn)(void *arg), void *arg);
void eyes_panel_join(void);
/* Like join, but never waits: true when no job is running (or it has just finished), false while
 * one is still running. */
bool eyes_panel_try_join(void);
