#pragma once

/* Panel is mounted rotated 90° in the arix body; every screen rotates its own content by this
 * (clockwise, as an LVGL transform_rotation would): the eyes rasterize pre-rotated pixels, the
 * clock pre-rotates its face once per redraw, and small static items may use a single LVGL
 * transform (never nested). Full-screen LVGL transforms are far too slow to animate here. */
#define UI_ROTATION_DEG 90
