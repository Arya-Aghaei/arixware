#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "ui_core.h"
#include "audio.h"
#include "mmap_generate_sounds.h"

/**
 * Creates the settings shade. It takes direct ownership of the panel for its whole life (released
 * when the screen is deleted) and composes its first frame without showing it: bring it on screen
 * with a cross-fade to ui_screen_settings_panel_pixels(), then load the screen.
 */
lv_obj_t *ui_screen_settings_create(const ui_settings_controls_t *controls);

/** Builds the static artwork caches ahead of the first opening (~0.5 s of drawing); call while
 *  nothing animates, e.g. during the boot splash. */
void ui_screen_settings_prewarm(void);

/** The current frame: native RGB565, 466x466, panel orientation. NULL if not created. */
const uint16_t *ui_screen_settings_panel_pixels(void);

/** True while a slider drag, information scroll or page change owns the touch, so gestures must be ignored. */
bool ui_screen_settings_touch_captured(void);

/** Returns to the main page from a sub-page; false if already on the main page. */
bool ui_screen_settings_back(void);

/** Stops input and animation before the shade is faded out and deleted. */
void ui_screen_settings_freeze(void);
