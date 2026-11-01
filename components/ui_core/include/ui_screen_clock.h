#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_screen_clock_create(void);
/** The clock face exactly as it will appear on the panel (native RGB565, 466x466, already rotated),
 *  valid while the clock screen exists. */
const uint16_t *ui_screen_clock_panel_pixels(void);

#ifdef __cplusplus
}
#endif
