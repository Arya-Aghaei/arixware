#include "net_remote.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"

#define CODE_DIGITS 6
#define MAX_FAILURES 5
#define LOCKOUT_US (60 * 1000000LL)
#define MOOD_COUNT 30

static const char *TAG = "net_remote";

extern const char app_html_start[] asm("_binary_app_html_start");
extern const char app_html_end[] asm("_binary_app_html_end");
extern const char manifest_json_start[] asm("_binary_manifest_json_start");
extern const char manifest_json_end[] asm("_binary_manifest_json_end");
extern const uint8_t icon_png_start[] asm("_binary_icon_png_start");
extern const uint8_t icon_png_end[] asm("_binary_icon_png_end");
extern const uint8_t icon_192_png_start[] asm("_binary_icon_192_png_start");
extern const uint8_t icon_192_png_end[] asm("_binary_icon_192_png_end");
extern const uint8_t eyes_png_start[] asm("_binary_eyes_png_start");
extern const uint8_t eyes_png_end[] asm("_binary_eyes_png_end");

static net_remote_ops_t s_ops;
static char s_code[CODE_DIGITS + 1];
static int s_failures;
static int64_t s_locked_until_us;

void net_remote_set_ops(const net_remote_ops_t *ops)
{
    s_ops = *ops;
}

static void load_code(void)
{
    if (s_code[0]) {
        return;
    }
    nvs_handle_t handle;
    uint32_t code = 0;
    if (nvs_open("remote", NVS_READWRITE, &handle) == ESP_OK) {
        if (nvs_get_u32(handle, "code", &code) != ESP_OK || code < 100000 || code > 999999) {
            code = 100000 + esp_random() % 900000;
            nvs_set_u32(handle, "code", code);
            nvs_commit(handle);
        }
        nvs_close(handle);
    } else {
        code = 100000 + esp_random() % 900000; /* not persisted, but still protects this session */
    }
    snprintf(s_code, sizeof(s_code), "%06u", (unsigned)code);
}

void net_remote_code(char out[7])
{
    load_code();
    memcpy(out, s_code, sizeof(s_code));
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *body = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

/* Checks the X-Code header. Wrong guesses are rate limited so the 6 digits cannot be brute forced. */
static bool authorized(httpd_req_t *req)
{
    load_code();
    int64_t now = esp_timer_get_time();
    if (now < s_locked_until_us) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_sendstr(req, "Too many wrong codes. Wait a minute.");
        return false;
    }
    char code[CODE_DIGITS + 4];
    if (httpd_req_get_hdr_value_str(req, "X-Code", code, sizeof(code)) == ESP_OK && !strcmp(code, s_code)) {
        s_failures = 0;
        return true;
    }
    if (++s_failures >= MAX_FAILURES) {
        s_failures = 0;
        s_locked_until_us = now + LOCKOUT_US;
        ESP_LOGW(TAG, "Locked remote for a minute after wrong codes");
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "Wrong code");
    return false;
}

static esp_err_t app_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, app_html_start, app_html_end - app_html_start - 1);
}

/* The manifest is a template: the start URL carries the remote code when the request proves it knows
 * it (?pair=), so an app added to the home screen opens already paired (iOS keeps separate storage). */
static esp_err_t manifest_handler(httpd_req_t *req)
{
    load_code();
    char query[32] = "";
    char pair[16] = "";
    bool paired = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
                  httpd_query_key_value(query, "pair", pair, sizeof(pair)) == ESP_OK && !strcmp(pair, s_code);
    char start[40];
    snprintf(start, sizeof(start), paired ? "/app?pair=%s" : "/app", s_code);
    const char *marker = "__START__";
    size_t length = (size_t)(manifest_json_end - manifest_json_start - 1);
    const char *at = strstr(manifest_json_start, marker);
    httpd_resp_set_type(req, "application/manifest+json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!at) {
        return httpd_resp_send(req, manifest_json_start, length);
    }
    httpd_resp_send_chunk(req, manifest_json_start, at - manifest_json_start);
    httpd_resp_send_chunk(req, start, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, at + strlen(marker), length - (size_t)(at - manifest_json_start) - strlen(marker));
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t icon_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)icon_png_start, icon_png_end - icon_png_start);
}

static esp_err_t eyes_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)eyes_png_start, eyes_png_end - eyes_png_start);
}

static esp_err_t icon_192_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)icon_192_png_start, icon_192_png_end - icon_192_png_start);
}

static cJSON *state_json(void)
{
    net_remote_state_t st = { 0 };
    if (s_ops.get_state) {
        s_ops.get_state(&st);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "brightness", st.brightness);
    cJSON_AddNumberToObject(root, "volume", st.volume);
    cJSON_AddBoolToObject(root, "dnd", st.dnd);
    cJSON_AddBoolToObject(root, "asleep", st.asleep);
    cJSON_AddNumberToObject(root, "mood", st.mood);
    cJSON *alarm = cJSON_AddObjectToObject(root, "alarm");
    cJSON_AddNumberToObject(alarm, "hour", st.alarm.hour);
    cJSON_AddNumberToObject(alarm, "minute", st.alarm.minute);
    cJSON_AddBoolToObject(alarm, "enabled", st.alarm.enabled);
    cJSON_AddBoolToObject(alarm, "ringing", st.alarm.ringing);
    cJSON *timer = cJSON_AddObjectToObject(root, "timer");
    cJSON_AddNumberToObject(timer, "state", st.timer.state);
    cJSON_AddNumberToObject(timer, "remaining", st.timer.remaining_s);
    cJSON_AddNumberToObject(timer, "total", st.timer.total_s);
    return root;
}

static esp_err_t state_handler(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    return send_json(req, state_json());
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool number(const cJSON *obj, const char *key, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out = (int)item->valuedouble;
    return true;
}

/* One JSON object may carry any of: mood, brightness, volume, dnd, alarm{hour,minute,enabled},
 * timer{minutes,start}, sleep (true sleeps, false wakes). Unknown keys are ignored. */
static esp_err_t command_handler(httpd_req_t *req)
{
    if (!authorized(req)) {
        return ESP_OK;
    }
    char body[256];
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
    }
    size_t used = 0;
    while (used < req->content_len) {
        int ret = httpd_req_recv(req, body + used, req->content_len - used);
        if (ret <= 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete request");
        }
        used += (size_t)ret;
    }
    body[used] = 0;
    cJSON *root = cJSON_Parse(body);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected a JSON object");
    }

    int value;
    if (number(root, "mood", &value) && s_ops.set_mood && value >= 0 && value < MOOD_COUNT) {
        s_ops.set_mood(value);
    }
    if (number(root, "brightness", &value) && s_ops.set_brightness) {
        s_ops.set_brightness(clamp(value, 5, 100));
    }
    if (number(root, "volume", &value) && s_ops.set_volume) {
        s_ops.set_volume(clamp(value, 0, 100));
    }
    const cJSON *dnd = cJSON_GetObjectItemCaseSensitive(root, "dnd");
    if (cJSON_IsBool(dnd) && s_ops.set_dnd) {
        s_ops.set_dnd(cJSON_IsTrue(dnd));
    }
    const cJSON *alarm = cJSON_GetObjectItemCaseSensitive(root, "alarm");
    int hour, minute;
    if (cJSON_IsObject(alarm) && s_ops.set_alarm && number(alarm, "hour", &hour) && number(alarm, "minute", &minute)) {
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(alarm, "enabled");
        s_ops.set_alarm(clamp(hour, 0, 23), clamp(minute, 0, 59), cJSON_IsTrue(enabled));
    }
    const cJSON *timer = cJSON_GetObjectItemCaseSensitive(root, "timer");
    int minutes;
    if (cJSON_IsObject(timer) && s_ops.set_timer && number(timer, "minutes", &minutes)) {
        s_ops.set_timer(clamp(minutes, 1, 99), cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(timer, "start")));
    }
    const cJSON *sleep = cJSON_GetObjectItemCaseSensitive(root, "sleep");
    if (cJSON_IsBool(sleep)) {
        if (cJSON_IsTrue(sleep) && s_ops.sleep) {
            s_ops.sleep();
        } else if (cJSON_IsFalse(sleep) && s_ops.wake) {
            s_ops.wake();
        }
    }
    cJSON_Delete(root);
    return send_json(req, state_json());
}

void net_remote_register(void *httpd_handle)
{
    httpd_handle_t server = httpd_handle;
    load_code();
    const httpd_uri_t routes[] = {
        { .uri = "/app", .method = HTTP_GET, .handler = app_handler },
        { .uri = "/app/manifest.json", .method = HTTP_GET, .handler = manifest_handler },
        { .uri = "/app/icon.png", .method = HTTP_GET, .handler = icon_handler },
        { .uri = "/app/eyes.png", .method = HTTP_GET, .handler = eyes_handler },
        { .uri = "/app/icon-192.png", .method = HTTP_GET, .handler = icon_192_handler },
        { .uri = "/api/remote", .method = HTTP_GET, .handler = state_handler },
        { .uri = "/api/remote", .method = HTTP_POST, .handler = command_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    }
}
