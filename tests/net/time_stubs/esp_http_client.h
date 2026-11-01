#pragma once
#include "esp_err.h"
#include <stdbool.h>
typedef void *esp_http_client_handle_t;
#define HTTP_EVENT_ON_DATA 1
typedef struct { int event_id; void *user_data; void *data; int data_len; } esp_http_client_event_t;
typedef struct {
    const char *url;
    int timeout_ms;
    bool disable_auto_redirect;
    esp_err_t (*crt_bundle_attach)(void *);
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
