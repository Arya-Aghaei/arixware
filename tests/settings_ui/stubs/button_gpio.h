#pragma once
#include "iot_button.h"
typedef struct { int gpio_num; int active_level; } button_gpio_config_t;
esp_err_t iot_button_new_gpio_device(const button_config_t *config,
                                     const button_gpio_config_t *gpio, button_handle_t *handle);
