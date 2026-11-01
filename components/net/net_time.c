#include "net_time.h"
#include "net_timezone.h"
#include "net_wifi.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

static const char *TAG = "net_time";
static TaskHandle_t s_task;
typedef struct { char text[512]; size_t used; bool overflow; } response_t;

static esp_err_t response_cb(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    response_t *body = event->user_data;
    if (event->data_len < 0 || (size_t)event->data_len >= sizeof(body->text)-body->used) {
        body->overflow = true;
        return ESP_FAIL;
    }
    memcpy(body->text+body->used, event->data, event->data_len);
    body->used += event->data_len;
    body->text[body->used] = 0;
    return ESP_OK;
}

static bool detect_timezone(void)
{
    response_t body = {0};
    const esp_http_client_config_t config = {
        .url = "https://ipwho.is/?fields=success,timezone.id",
        .timeout_ms = 5000,
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = response_cb,
        .user_data = &body,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    esp_err_t result = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (result != ESP_OK || status != 200 || !body.used || body.overflow) return false;
    cJSON *json = cJSON_Parse(body.text);
    cJSON *success = cJSON_GetObjectItemCaseSensitive(json, "success");
    cJSON *timezone = cJSON_GetObjectItemCaseSensitive(json, "timezone");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(timezone, "id");
    bool applied = cJSON_IsTrue(success) && cJSON_IsString(name) && net_timezone_set(name->valuestring);
    if (applied) ESP_LOGI(TAG, "Detected timezone: %s", name->valuestring);
    cJSON_Delete(json);
    return applied;
}

static void timezone_task(void *unused)
{
    (void)unused;
    TickType_t wait = portMAX_DELAY;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, wait);
        net_wifi_status_t status;
        net_wifi_get_status(&status);
        if (status.state != NET_WIFI_CONNECTED) { wait = portMAX_DELAY; continue; }
        /* TLS validation requires a valid UTC clock. SNTP starts independently. */
        for (int n = 0; n < 20 && time(NULL) < 1577836800; n++) vTaskDelay(pdMS_TO_TICKS(1000));
        bool detected = time(NULL) >= 1577836800 && detect_timezone();
        if (!detected) ESP_LOGW(TAG, "Timezone lookup unavailable; retaining saved timezone");
        /* pdMS_TO_TICKS multiplies in 32 bits; six hours in ms would overflow at 1 kHz. */
        wait = detected ? (TickType_t)(6u*60u*60u*configTICK_RATE_HZ) : pdMS_TO_TICKS(60000);
    }
}

void net_time_connected(void)
{
    /* Only the Wi-Fi worker calls this; the task initially waits for this notification. */
    if (!s_task && xTaskCreate(timezone_task, "timezone", 6144, NULL, 2, &s_task) != pdPASS) {
        ESP_LOGW(TAG, "Could not start timezone detection");
        return;
    }
    xTaskNotifyGive(s_task);
}
