#pragma once
#include <stdint.h>

#define PANEL_HOST_W 466
#define PANEL_HOST_H 466

/* RGB565 byte-swapped, as sent to the panel. */
const uint16_t *panel_host_fb(void);
unsigned long panel_host_blit_pixels(void);
