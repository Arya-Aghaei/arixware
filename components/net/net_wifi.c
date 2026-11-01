#include "net_wifi.h"
#include "net_time.h"
#include "net_timezone.h"
#include "net_profile.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#define WIFI_NVS_NAMESPACE "wifi_cfg"
#define GOT_IP BIT0
#define DISCONNECTED BIT1
#define CONNECT_TIMEOUT_US 20000000LL

#ifndef WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
#define WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT 15
#endif
#ifndef WIFI_REASON_NO_AP_FOUND
#define WIFI_REASON_NO_AP_FOUND 201
#endif
#ifndef WIFI_REASON_AUTH_FAIL
#define WIFI_REASON_AUTH_FAIL 202
#endif
#ifndef WIFI_REASON_HANDSHAKE_TIMEOUT
#define WIFI_REASON_HANDSHAKE_TIMEOUT 204
#endif
#ifndef WIFI_REASON_CONNECTION_FAIL
#define WIFI_REASON_CONNECTION_FAIL 205
#endif

static const char *TAG = "net_wifi";
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_commands;
static EventGroupHandle_t s_events;
static atomic_bool s_time_synced;
static net_wifi_status_t s_status = { .state = NET_WIFI_STARTING };
static net_wifi_scan_t s_scan;
static bool s_sntp_started;
static volatile uint8_t s_disconnect_reason = 0;

typedef enum { CMD_SETUP, CMD_SCAN, CMD_CONNECT } command_type_t;
typedef struct {
    command_type_t type;
    char ssid[33];
    char pass[65];
    char owner_name[NET_PROFILE_OWNER_MAX + 1];
} command_t;

void net_wifi_get_status(net_wifi_status_t *status)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    if (s_lock) xSemaphoreGive(s_lock);
    status->time_synced = atomic_load(&s_time_synced);
}

void net_wifi_get_scan(net_wifi_scan_t *scan)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *scan = s_scan;
    if (s_lock) xSemaphoreGive(s_lock);
}

static void set_state(net_wifi_state_t state, const char *error)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = state;
    snprintf(s_status.error, sizeof(s_status.error), "%s", error ? error : "");
    if (state != NET_WIFI_CONNECTED) s_status.ip[0] = '\0';
    xSemaphoreGive(s_lock);
}

static void time_sync_cb(struct timeval *tv)
{
    (void)tv;
    atomic_store(&s_time_synced, true);
}

static void start_sntp_once(void)
{
    if (s_sntp_started) return;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = time_sync_cb;
    if (esp_netif_sntp_init(&config) == ESP_OK) s_sntp_started = true;
}

static esp_err_t save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    /* One blob prevents a failed write from mixing a new SSID with an old password. */
    char credentials[98] = { 0 };
    snprintf(credentials, 33, "%s", ssid);
    snprintf(credentials + 33, 65, "%s", pass);
    err = nvs_set_blob(handle, "credentials", credentials, sizeof(credentials));
    if (err == ESP_OK) err = nvs_commit(handle);
    memset(credentials, 0, sizeof(credentials));
    nvs_close(handle);
    return err;
}

static bool load_credentials(command_t *cmd)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    char credentials[98];
    size_t length = sizeof(credentials);
    if (nvs_get_blob(handle, "credentials", credentials, &length) == ESP_OK && length == sizeof(credentials) &&
        credentials[0] && memchr(credentials, '\0', 33) && memchr(credentials + 33, '\0', 65)) {
        memcpy(cmd->ssid, credentials, 33);
        memcpy(cmd->pass, credentials + 33, 65);
        memset(credentials, 0, sizeof(credentials));
        nvs_close(handle);
        return true;
    }
    /* Continue to read credentials saved by older firmware. */
    memset(credentials, 0, sizeof(credentials));
    size_t ssid_len = sizeof(cmd->ssid), pass_len = sizeof(cmd->pass);
    esp_err_t a = nvs_get_str(handle, "ssid", cmd->ssid, &ssid_len);
    esp_err_t b = nvs_get_str(handle, "pass", cmd->pass, &pass_len);
    nvs_close(handle);
    return a == ESP_OK && b == ESP_OK && cmd->ssid[0];
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.setup_client_ready = true;
        xSemaphoreGive(s_lock);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_sta_list_t stations;
        if (esp_wifi_ap_get_sta_list(&stations) == ESP_OK && stations.num == 0) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_status.setup_client_ready = false;
            xSemaphoreGive(s_lock);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (data) {
            const wifi_event_sta_disconnected_t *disconn = (const wifi_event_sta_disconnected_t *)data;
            s_disconnect_reason = disconn->reason;
        }
        xEventGroupSetBits(s_events, DISCONNECTED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&event->ip_info.ip));
        xSemaphoreGive(s_lock);
        xEventGroupSetBits(s_events, GOT_IP);
    }
}

static bool enable_setup(void)
{
    net_wifi_status_t status;
    net_wifi_get_status(&status);
    if (status.setup_active) return true;
    wifi_config_t ap = { .ap = { .channel = 1, .max_connection = 2, .authmode = WIFI_AUTH_WPA2_PSK } };
    memcpy(ap.ap.ssid, status.setup_ssid, strlen(status.setup_ssid));
    ap.ap.ssid_len = strlen(status.setup_ssid);
    memcpy(ap.ap.password, status.setup_password, strlen(status.setup_password));
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.setup_active = true;
    s_status.setup_client_ready = false;
    s_status.setup_complete = false;
    if (s_status.state != NET_WIFI_CONNECTED && s_status.state != NET_WIFI_CONNECTING) s_status.state = NET_WIFI_SETUP;
    xSemaphoreGive(s_lock);
    return true;
}

static void scan_networks(void)
{
    net_wifi_scan_t result = { 0 };
    wifi_scan_config_t cfg = { 0 };
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) {
        snprintf(result.error, sizeof(result.error), "Scan failed. Try again after connecting finishes.");
    } else {
        wifi_ap_record_t records[NET_WIFI_SCAN_MAX];
        uint16_t count = NET_WIFI_SCAN_MAX;
        if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
            snprintf(result.error, sizeof(result.error), "Could not read nearby networks.");
        } else {
            for (int i = 0; i < count; i++) {
                if (!records[i].ssid[0]) continue;
                bool duplicate = false;
                for (int j = 0; j < result.count; j++) {
                    if (!strncmp(result.networks[j].ssid, (char *)records[i].ssid, 32)) duplicate = true;
                }
                if (duplicate) continue;
                net_wifi_network_t *network = &result.networks[result.count++];
                snprintf(network->ssid, sizeof(network->ssid), "%.*s", 32, records[i].ssid);
                network->rssi = records[i].rssi;
                network->secured = records[i].authmode != WIFI_AUTH_OPEN;
            }
        }
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_scan = result;
    xSemaphoreGive(s_lock);
}

static bool begin_connection(const command_t *cmd)
{
    s_disconnect_reason = 0;
    /* Consume the old session's disconnect before starting the new attempt. */
    xEventGroupClearBits(s_events, GOT_IP | DISCONNECTED);
    if (esp_wifi_disconnect() == ESP_OK) {
        xEventGroupWaitBits(s_events, DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
    }
    xEventGroupClearBits(s_events, GOT_IP | DISCONNECTED);
    wifi_config_t sta = { 0 };
    memcpy(sta.sta.ssid, cmd->ssid, strlen(cmd->ssid));
    memcpy(sta.sta.password, cmd->pass, strlen(cmd->pass));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", cmd->ssid);
    xSemaphoreGive(s_lock);
    set_state(NET_WIFI_CONNECTING, NULL);
    return esp_wifi_set_config(WIFI_IF_STA, &sta) == ESP_OK && esp_wifi_connect() == ESP_OK;
}

static void wifi_worker(void *arg)
{
    (void)arg;
    command_t candidate = { .type = CMD_CONNECT };
    bool connecting = false, save_on_success = false;
    int attempts = 0;
    int64_t deadline = 0;
    int64_t close_setup_at = 0;
    if (load_credentials(&candidate)) {
        connecting = begin_connection(&candidate);
        deadline = esp_timer_get_time() + CONNECT_TIMEOUT_US;
    }
    if (!connecting) {
        enable_setup();
        set_state(NET_WIFI_SETUP, NULL);
    }
    for (;;) {
        command_t cmd;
        if (xQueueReceive(s_commands, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (cmd.type == CMD_SETUP) {
                close_setup_at = 0;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_status.setup_complete = false;
                xSemaphoreGive(s_lock);
                if (!enable_setup()) set_state(NET_WIFI_DISCONNECTED, "Could not start setup network.");
            } else if (cmd.type == CMD_SCAN) {
                scan_networks();
            } else {
                candidate = cmd;
                close_setup_at = 0;
                save_on_success = true;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_status.setup_complete = false;
                xSemaphoreGive(s_lock);
                attempts = 0;
                connecting = begin_connection(&candidate);
                deadline = esp_timer_get_time() + CONNECT_TIMEOUT_US;
                if (!connecting) {
                    set_state(NET_WIFI_SETUP, "Could not start connection. Check your network and retry.");
                }
            }
            memset(&cmd, 0, sizeof(cmd));
        }
        EventBits_t events = xEventGroupWaitBits(s_events, GOT_IP | DISCONNECTED, pdTRUE, pdFALSE, 0);
        if (connecting && (events & GOT_IP)) {
            esp_err_t saved = save_on_success ? save_credentials(candidate.ssid, candidate.pass) : ESP_OK;
            if (save_on_success && saved == ESP_OK) saved = net_profile_save_owner(candidate.owner_name);
            if (save_on_success) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_status.setup_complete = saved == ESP_OK;
                xSemaphoreGive(s_lock);
            }
            set_state(NET_WIFI_CONNECTED, saved == ESP_OK ? NULL : "Connected, but could not save. Please retry.");
            connecting = false;
            save_on_success = false;
            attempts = 0;
            start_sntp_once();
            net_time_connected();
            /* Keep setup active for 15s to allow phone captive portal to display connection confirmation. */
            if (saved == ESP_OK) close_setup_at = esp_timer_get_time() + 15000000LL;
            ESP_LOGI(TAG, "Wi-Fi connected");
        } else if (connecting && ((events & DISCONNECTED) || esp_timer_get_time() >= deadline)) {
            bool is_auth_fail = (s_disconnect_reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                                 s_disconnect_reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                                 s_disconnect_reason == WIFI_REASON_AUTH_FAIL ||
                                 s_disconnect_reason == WIFI_REASON_CONNECTION_FAIL);
            int max_attempts = is_auth_fail ? 2 : 3;
            if (++attempts < max_attempts && (events & DISCONNECTED) && esp_wifi_connect() == ESP_OK) {
                deadline = esp_timer_get_time() + CONNECT_TIMEOUT_US;
            } else {
                connecting = false;
                esp_wifi_disconnect();
                enable_setup();
                const char *error_msg = "Connection failed. Check the password and 2.4 GHz network, then retry.";
                if (is_auth_fail) {
                    error_msg = "Incorrect Wi-Fi password. Check the password and try again.";
                } else if (s_disconnect_reason == WIFI_REASON_NO_AP_FOUND) {
                    error_msg = "Network not found. Make sure it is in range and is 2.4 GHz.";
                }
                set_state(NET_WIFI_SETUP, error_msg);
            }
        } else if (!connecting && (events & DISCONNECTED)) {
            net_wifi_status_t status;
            net_wifi_get_status(&status);
            if (status.state == NET_WIFI_CONNECTED) {
                set_state(NET_WIFI_DISCONNECTED, NULL);
                attempts = 0;
                connecting = esp_wifi_connect() == ESP_OK;
                deadline = esp_timer_get_time() + CONNECT_TIMEOUT_US;
                if (!connecting) { enable_setup(); set_state(NET_WIFI_SETUP, "Network lost. Set up Wi-Fi again."); }
            }
        }
        if (close_setup_at && esp_timer_get_time() >= close_setup_at) {
            net_wifi_status_t status;
            net_wifi_get_status(&status);
            if (status.state == NET_WIFI_CONNECTED && esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_status.setup_active = false;
                xSemaphoreGive(s_lock);
            }
            close_setup_at = 0;
        }
    }
}

bool net_wifi_start_setup(void)
{
    command_t cmd = { .type = CMD_SETUP };
    return s_commands && xQueueSend(s_commands, &cmd, 0) == pdTRUE;
}

bool net_wifi_start_scan(void)
{
    if (!s_commands) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool available = s_status.setup_active && s_status.state != NET_WIFI_CONNECTING && !s_scan.scanning;
    if (available) s_scan.scanning = true;
    xSemaphoreGive(s_lock);
    if (!available) return false;
    command_t cmd = { .type = CMD_SCAN };
    if (xQueueSend(s_commands, &cmd, 0) == pdTRUE) return true;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_scan.scanning = false;
    xSemaphoreGive(s_lock);
    return false;
}

bool net_wifi_connect(const char *ssid, const char *pass, const char *owner_name)
{
    if (!ssid || !pass || !ssid[0] || strlen(ssid) > 32 || strlen(pass) > 63 || (pass[0] && strlen(pass) < 8)) return false;
    if (!s_commands) return false;
    command_t cmd = { .type = CMD_CONNECT };
    if (!net_profile_normalize_owner(owner_name, cmd.owner_name)) return false;
    snprintf(cmd.ssid, sizeof(cmd.ssid), "%s", ssid);
    snprintf(cmd.pass, sizeof(cmd.pass), "%s", pass);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool queued = s_status.setup_active && s_status.state != NET_WIFI_CONNECTING &&
                  xQueueSend(s_commands, &cmd, 0) == pdTRUE;
    if (queued) {
        s_status.setup_complete = false;
        s_status.state = NET_WIFI_CONNECTING;
        s_status.error[0] = '\0';
        s_status.ip[0] = '\0';
    }
    xSemaphoreGive(s_lock);
    memset(&cmd, 0, sizeof(cmd));
    return queued;
}

void net_wifi_init(void)
{
    net_timezone_init();
    s_lock = xSemaphoreCreateMutex();
    s_commands = xQueueCreate(3, sizeof(command_t));
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(s_lock && s_commands && s_events ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    /* Configure local DNS before the first client obtains its DHCP lease.
     * Do not advertise option 114: it requires an HTTPS RFC 8908 API, not our
     * HTTP HTML page. Android can instead detect the HTTP probe redirects. */
    esp_err_t stopped = esp_netif_dhcps_stop(ap_netif);
    ESP_ERROR_CHECK(stopped == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED ? ESP_OK : stopped);
    esp_netif_dns_info_t dns = { .ip = ESP_IP4ADDR_INIT(192, 168, 4, 1) };
    ESP_ERROR_CHECK(esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns));
    uint8_t offer_dns = 1; /* ESP_NETIF_OP_SET accepts a boolean. */
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                        &offer_dns, sizeof(offer_dns)));
    /* Re-arm DHCP now; with the AP down this restores INIT, so the default
     * AP-start handler will begin offering leases when the hotspot comes up. */
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    /* This component owns persistence; trial credentials must never be stored by the driver. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    uint8_t mac[6], random[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    snprintf(s_status.setup_ssid, sizeof(s_status.setup_ssid), "Arixware-Setup-%02X%02X", mac[4], mac[5]);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_fill_random(random, sizeof(random));
    for (int i = 0; i < 6; i++) snprintf(s_status.setup_password + i * 2, 3, "%02x", random[i]);
    ESP_ERROR_CHECK(xTaskCreate(wifi_worker, "wifi_setup", 6144, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
