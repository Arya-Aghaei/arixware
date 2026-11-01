#pragma once
#include <stdint.h>

typedef int esp_err_t;
typedef int nvs_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define NVS_READONLY 0
#define NVS_READWRITE 1

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
const char *esp_err_to_name(esp_err_t err);
