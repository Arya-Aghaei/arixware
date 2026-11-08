#pragma once
#include "lvgl.h"
#include <stdint.h>

/** Firmware update screen in the clock/timer style: the two orb eyes, a glowing percentage and a
 *  progress ring around the bezel. Only one can exist at a time. */
lv_obj_t *ui_screen_update_create(const char *version);

/** 0-100. Does nothing when the screen does not exist. */
void ui_screen_update_set_progress(int percent);

/** Switch to the failure or success look. */
void ui_screen_update_set_failed(const char *reason);
void ui_screen_update_set_done(void);
