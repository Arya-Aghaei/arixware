#include "net_ota.h"
#include "net_wifi.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELEASE_MAX (48 * 1024)
#define VALID_AFTER_US (20 * 1000000LL)

static const char *TAG = "net_ota";
static net_ota_ops_t s_ops;
static net_ota_status_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_running;
static bool s_auto = true;
static TaskHandle_t s_auto_task;

typedef struct { char *data; size_t used; bool overflow; } body_t;

void net_ota_set_ops(const net_ota_ops_t *ops)
{
    s_ops = *ops;
}

const char *net_ota_running_version(void)
{
    return esp_app_get_description()->version;
}

void net_ota_get_status(net_ota_status_t *status)
{
    taskENTER_CRITICAL(&s_lock);
    *status = s_status;
    taskEXIT_CRITICAL(&s_lock);
}

static void set_state(net_ota_state_t state, int progress, const char *latest, const char *error)
{
    taskENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.progress = progress;
    if (latest) snprintf(s_status.latest, sizeof(s_status.latest), "%s", latest);
    snprintf(s_status.error, sizeof(s_status.error), "%s", error ? error : "");
    taskEXIT_CRITICAL(&s_lock);
}

/* The install itself failed: the update screen is up and shows the reason. */
static void fail(const char *reason)
{
    ESP_LOGW(TAG, "Update failed: %s", reason);
    set_state(NET_OTA_FAILED, 0, NULL, reason);
    if (s_ops.failed) s_ops.failed(reason);
}

/* Parses "v1.2.3" or "1.2.3-extra" into three numbers. Anything else (a bare git hash such as
 * "4ca4dd3-dirty" from an untagged local build) counts as 0.0.0. */
static bool parse_version(const char *text, int out[3])
{
    out[0] = out[1] = out[2] = 0;
    if (*text == 'v' || *text == 'V') text++;
    char *end;
    long first = strtol(text, &end, 10);
    if (end == text || *end != '.') return false;
    out[0] = (int)first;
    text = end + 1;
    for (int i = 1; i < 3 && *text; i++) {
        out[i] = (int)strtol(text, &end, 10);
        if (*end != '.') break;
        text = end + 1;
    }
    return true;
}

static bool is_newer(const char *remote, const char *local)
{
    int a[3], b[3];
    parse_version(remote, a);
    parse_version(local, b);
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

static esp_err_t body_cb(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    body_t *body = event->user_data;
    if (event->data_len < 0 || body->used + (size_t)event->data_len >= RELEASE_MAX) {
        body->overflow = true;
        return ESP_FAIL;
    }
    memcpy(body->data + body->used, event->data, event->data_len);
    body->used += event->data_len;
    body->data[body->used] = 0;
    return ESP_OK;
}

/* Fetches the latest release; fills tag and the asset download URL. */
static bool fetch_release(char *tag, size_t tag_size, char *url, size_t url_size)
{
    body_t body = { .data = malloc(RELEASE_MAX) };
    if (!body.data) return false;
    body.data[0] = 0;
    char api[160];
    snprintf(api, sizeof(api), "https://api.github.com/repos/%s/releases/latest", CONFIG_NET_OTA_REPO);
    const esp_http_client_config_t config = {
        .url = api,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = body_cb,
        .user_data = &body,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    bool ok = false;
    if (client) {
        esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
        esp_http_client_set_header(client, "User-Agent", "arixware-ota");
        esp_err_t result = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        if (result == ESP_OK && status == 200 && !body.overflow) {
            cJSON *json = cJSON_Parse(body.data);
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(json, "tag_name");
            const cJSON *assets = cJSON_GetObjectItemCaseSensitive(json, "assets");
            const cJSON *asset;
            cJSON_ArrayForEach(asset, assets) {
                const cJSON *asset_name = cJSON_GetObjectItemCaseSensitive(asset, "name");
                const cJSON *download = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
                if (cJSON_IsString(name) && cJSON_IsString(asset_name) && cJSON_IsString(download) &&
                    !strcmp(asset_name->valuestring, CONFIG_NET_OTA_ASSET) &&
                    strlen(name->valuestring) < tag_size && strlen(download->valuestring) < url_size) {
                    strcpy(tag, name->valuestring);
                    strcpy(url, download->valuestring);
                    ok = true;
                    break;
                }
            }
            cJSON_Delete(json);
        } else {
            ESP_LOGW(TAG, "Release lookup: %s, HTTP %d", esp_err_to_name(result), status);
        }
        esp_http_client_cleanup(client);
    }
    free(body.data);
    return ok;
}

static bool install(const char *url)
{
    const esp_http_client_config_t http = {
        .url = url,
        .timeout_ms = 20000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .buffer_size = 4096,     /* the redirect Location header is long */
        .buffer_size_tx = 2048,
    };
    const esp_https_ota_config_t config = { .http_config = &http };
    esp_https_ota_handle_t handle = NULL;
    if (esp_https_ota_begin(&config, &handle) != ESP_OK) {
        fail("Download did not start");
        return false;
    }
    int total = esp_https_ota_get_image_size(handle);
    int last = -1;
    esp_err_t err;
    while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int percent = total > 0 ? (int)((int64_t)esp_https_ota_get_image_len_read(handle) * 100 / total) : 0;
        if (percent != last) {
            last = percent;
            taskENTER_CRITICAL(&s_lock);
            s_status.progress = percent;
            taskEXIT_CRITICAL(&s_lock);
            if (s_ops.progress) s_ops.progress(percent);
        }
    }
    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        esp_https_ota_abort(handle);
        fail("Download interrupted");
        return false;
    }
    if (esp_https_ota_finish(handle) != ESP_OK) {
        fail("Image rejected");
        return false;
    }
    return true;
}

static void ota_task(void *unused)
{
    (void)unused;
    char tag[32], url[512];
    if (!fetch_release(tag, sizeof(tag), url, sizeof(url))) {
        set_state(NET_OTA_FAILED, 0, NULL, "No release found");
    } else if (!is_newer(tag, net_ota_running_version())) {
        ESP_LOGI(TAG, "Up to date (%s, release %s)", net_ota_running_version(), tag);
        set_state(NET_OTA_CURRENT, 0, tag, NULL);
    } else {
        ESP_LOGI(TAG, "Installing %s", tag);
        set_state(NET_OTA_INSTALLING, 0, tag, NULL);
        if (s_ops.begin) s_ops.begin(tag);
        if (install(url)) {
            set_state(NET_OTA_DONE, 100, tag, NULL);
            if (s_ops.progress) s_ops.progress(100);
            if (s_ops.done) s_ops.done();
            vTaskDelay(pdMS_TO_TICKS(2500));
            esp_restart();
        }
    }
    s_running = false;
    vTaskDelete(NULL);
}

static bool start(void)
{
    net_wifi_status_t wifi;
    net_wifi_get_status(&wifi);
    if (s_running || wifi.state != NET_WIFI_CONNECTED) return false;
    s_running = true;
    set_state(NET_OTA_CHECKING, 0, NULL, NULL);
    if (xTaskCreate(ota_task, "ota", 10240, NULL, 3, NULL) != pdPASS) {
        s_running = false;
        set_state(NET_OTA_IDLE, 0, NULL, NULL);
        return false;
    }
    return true;
}

bool net_ota_start(void)
{
    return start();
}

bool net_ota_get_auto(void)
{
    return s_auto;
}

void net_ota_set_auto(bool enabled)
{
    s_auto = enabled;
    nvs_handle_t handle;
    if (nvs_open("ota", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "auto", enabled);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

/* First check two minutes after boot, then every day. A check that finds nothing is silent; an
 * install shows the update screen. Skipped while an alarm rings or a timer runs. */
static void auto_task(void *unused)
{
    (void)unused;
    vTaskDelay(pdMS_TO_TICKS(120000));
    for (;;) {
        bool ran = false;
        /* A local build (version is a git hash, not a release tag) never updates itself. */
        int version[3];
        bool release = parse_version(net_ota_running_version(), version);
        if (s_auto && release && !(s_ops.busy && s_ops.busy())) ran = start();
        for (int minutes = ran ? 24 * 60 : 5; minutes > 0; minutes--) vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

static void valid_cb(void *unused)
{
    (void)unused;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "Running image confirmed");
    }
}

void net_ota_init(void)
{
    /* Rollback is enabled: an image that crashes before this timer fires is replaced by the old one. */
    static esp_timer_handle_t timer;
    const esp_timer_create_args_t args = { .callback = valid_cb, .name = "ota_valid" };
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        esp_timer_start_once(timer, VALID_AFTER_US);
    }
    nvs_handle_t handle;
    uint8_t value;
    if (nvs_open("ota", NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_u8(handle, "auto", &value) == ESP_OK) s_auto = value != 0;
        nvs_close(handle);
    }
    if (!s_auto_task) xTaskCreate(auto_task, "ota_auto", 3072, NULL, 2, &s_auto_task);
}
