#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Updates the shared standby state without firing the physical-button callback. */
void power_button_set_standby(bool standby);

/** Watches the BOOT button (GPIO0); each press toggles standby and fires cb with the new state. */
void power_button_init(void (*on_toggle)(bool standby));

#ifdef __cplusplus
}
#endif
