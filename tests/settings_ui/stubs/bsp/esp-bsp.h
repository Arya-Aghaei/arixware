#pragma once
#include "nvs.h"

esp_err_t bsp_display_brightness_set(int value);
int bsp_display_brightness_get(void);
esp_err_t bsp_display_backlight_off(void);
esp_err_t bsp_display_lock(int timeout_ms);
void bsp_display_unlock(void);
