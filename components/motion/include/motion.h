#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Starts polling the onboard IMU for pickup/handling events. Call after display bring-up. */
void motion_init(void);

/** Registers the callback fired when a pickup/handling event is detected. */
void motion_set_pickup_cb(void (*cb)(void));

/** Registers a callback for filtered acceleration after repeated shake reversals. */
void motion_set_shake_cb(void (*cb)(float accel_x, float accel_y, bool active));

#ifdef __cplusplus
}
#endif
