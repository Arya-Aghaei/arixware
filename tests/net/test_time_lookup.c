#include "../../components/net/net_time.c"
#include <assert.h>
#include <stdio.h>
#include <setjmp.h>

static jmp_buf stop_loop;
static bool exercise_loop, connected;
static int notifications;
static TickType_t waits[2];
static esp_http_client_config_t options;
static const char *payload;
static int http_status = 200, closed, applied;
static esp_err_t transport_error;
static bool no_memory;
static char zone[64] = "Europe/Berlin";
esp_err_t esp_crt_bundle_attach(void *unused) { (void)unused; return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    assert(!strncmp(config->url, "https://", 8));
    assert(config->crt_bundle_attach == esp_crt_bundle_attach);
    assert(config->timeout_ms <= 5000 && config->disable_auto_redirect);
    options = *config;
    return no_memory ? NULL : (void *)1;
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    assert(client == (void *)1);
    /* Exercise fragmented HTTP delivery through the production data callback. */
    size_t size = strlen(payload);
    for (size_t n = 0; n < size; n += 7) {
        esp_http_client_event_t event = {.event_id=HTTP_EVENT_ON_DATA, .user_data=options.user_data,
            .data=(void *)(payload+n), .data_len=(int)(size-n < 7 ? size-n : 7)};
        options.event_handler(&event); /* Also test clients that ignore callback errors. */
    }
    return transport_error;
}
int esp_http_client_get_status_code(esp_http_client_handle_t client) { assert(client); return http_status; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { assert(client); closed++; return ESP_OK; }
bool net_timezone_set(const char *value)
{
    if (strcmp(value,"Asia/Tokyo") && strcmp(value,"America/New_York")) return false;
    snprintf(zone, sizeof(zone), "%s", value); applied++; return true;
}
void net_wifi_get_status(net_wifi_status_t *status) { status->state = connected ? NET_WIFI_CONNECTED : NET_WIFI_DISCONNECTED; }
uint32_t ulTaskNotifyTake(int clear, TickType_t ticks)
{
    (void)clear;
    if (exercise_loop) {
        waits[notifications++] = ticks;
        if (notifications == 2) longjmp(stop_loop, 1);
    }
    return 0;
}
static void check_wait(TickType_t expected)
{
    notifications=0; exercise_loop=true;
    if (!setjmp(stop_loop)) timezone_task(NULL);
    exercise_loop=false;
    assert(waits[0] == portMAX_DELAY && waits[1] == expected);
}
void vTaskDelay(TickType_t ticks) { (void)ticks; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *out)
{ (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)out; return 0; }
void xTaskNotifyGive(TaskHandle_t task) { (void)task; }
int main(void)
{
    payload = "{\"success\":true,\"timezone\":{\"id\":\"Asia/Tokyo\"}}";
    assert(detect_timezone() && applied == 1 && closed == 1 && !strcmp(zone,"Asia/Tokyo"));
    const char *invalid[] = {"not json", "{}", "{\"success\":false}",
        "{\"success\":true,\"timezone\":{\"id\":4}}", "{\"success\":true,\"timezone\":{\"id\":\"Unknown/Zone\"}}"};
    for (size_t n=0; n<sizeof(invalid)/sizeof(invalid[0]); n++) { payload=invalid[n]; assert(!detect_timezone()); }
    payload = "{\"success\":true,\"timezone\":{\"id\":\"America/New_York\"}}";
    http_status=429; assert(!detect_timezone());
    http_status=200; transport_error=ESP_FAIL; assert(!detect_timezone());
    transport_error=ESP_OK; no_memory=true; assert(!detect_timezone()); no_memory=false;
    char oversized[700]; memset(oversized,' ',sizeof(oversized)-1); oversized[sizeof(oversized)-1]=0;
    memcpy(oversized,payload,strlen(payload)); payload=oversized; assert(!detect_timezone());
    assert(applied == 1 && !strcmp(zone,"Asia/Tokyo")); /* Failures keep the old zone. */
    payload="{\"success\":true,\"timezone\":{\"id\":\"Asia/Tokyo\"}}";
    connected=true; check_wait(21600000u); /* Six hours at the firmware's 1 kHz tick. */
    http_status=503; check_wait(60000u); /* Retry failures without blocking Wi-Fi/UI. */
    connected=false; check_wait(portMAX_DELAY); /* Resume on the next connection event. */
    puts("PASS: secure lookup, chunked/invalid replies, network failures and refresh/retry scheduling");
}
