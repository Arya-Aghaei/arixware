#pragma once
#include "nvs.h"
typedef void *button_handle_t;
typedef struct { int unused; } button_config_t;
#define BUTTON_SINGLE_CLICK 1
esp_err_t iot_button_register_cb(button_handle_t handle, int event, void *args,
                                void (*callback)(void *, void *), void *data);
