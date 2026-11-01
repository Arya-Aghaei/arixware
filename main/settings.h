#pragma once
#include <stdbool.h>

/** Restore saved controls and connect them to the settings UI. Call after audio_init(). */
void settings_init(void);

/** Apply shared display standby. Caller must hold the display lock. */
void settings_set_standby(bool standby);

/** Restore the selected brightness after display standby. */
void settings_restore_brightness(void);
