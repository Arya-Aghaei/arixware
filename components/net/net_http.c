#include "net_http.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "net_wifi.h"
#include "net_profile.h"
#include "net_remote.h"
#include "nvs.h"
#include "lwip/sockets.h"

static const char *TAG = "net_http";
extern const char setup_html_start[] asm("_binary_setup_html_start");
extern const char setup_html_end[] asm("_binary_setup_html_end");
void net_dns_start(void);
static esp_err_t captive_handler(httpd_req_t *req);

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *body = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

static bool setup_ipv4_address(const struct sockaddr_storage *address, socklen_t size, uint32_t *ipv4)
{
    if (address->ss_family == AF_INET && size >= sizeof(struct sockaddr_in)) {
        *ipv4 = ntohl(((const struct sockaddr_in *)address)->sin_addr.s_addr);
        return true;
    }
#if CONFIG_LWIP_IPV6
    if (address->ss_family == AF_INET6 && size >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *address6 = (const struct sockaddr_in6 *)address;
        const uint8_t mapped_prefix[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
        if (!memcmp(address6->sin6_addr.s6_addr, mapped_prefix, sizeof(mapped_prefix))) {
            uint32_t network_address;
            memcpy(&network_address, address6->sin6_addr.s6_addr + 12, sizeof(network_address));
            *ipv4 = ntohl(network_address);
            return true;
        }
    }
#endif
    return false;
}

/* Provisioning is accessible only through the physically authorized setup hotspot. */
static bool setup_client(httpd_req_t *req)
{
    net_wifi_status_t status;
    net_wifi_get_status(&status);
    /* ESP-IDF's dual-stack HTTP listener reports IPv4 clients as mapped IPv6. */
    struct sockaddr_storage peer = { 0 }, local = { 0 };
    socklen_t peer_size = sizeof(peer), local_size = sizeof(local);
    uint32_t peer_ip, local_ip;
    int fd = httpd_req_to_sockfd(req);
    bool from_setup = status.setup_active &&
                      getpeername(fd, (struct sockaddr *)&peer, &peer_size) == 0 &&
                      getsockname(fd, (struct sockaddr *)&local, &local_size) == 0 &&
                      setup_ipv4_address(&peer, peer_size, &peer_ip) &&
                      setup_ipv4_address(&local, local_size, &local_ip) &&
                      local_ip == 0xc0a80401U && (peer_ip & 0xffffff00U) == 0xc0a80400U;
    if (!from_setup) {
        ESP_LOGW(TAG, "Rejected setup API request: not a setup hotspot client");
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Join the setup Wi-Fi shown on the device first.");
        return false;
    }
    if (req->method == HTTP_POST) {
        char origin[64];
        size_t len = httpd_req_get_hdr_value_len(req, "Origin");
        if (len && (len >= sizeof(origin) || httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK ||
                    strcmp(origin, "http://192.168.4.1"))) {
            httpd_resp_set_status(req, "403 Forbidden");
            httpd_resp_sendstr(req, "Open http://192.168.4.1 to continue.");
            return false;
        }
    }
    return true;
}

static esp_err_t page_handler(httpd_req_t *req)
{
    net_wifi_status_t status;
    net_wifi_get_status(&status);
    char host[80];
    if (status.setup_active &&
        (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
         (strcmp(host, "192.168.4.1") && strcmp(host, "192.168.4.1:80")))) {
        /* Apple and other probes can request '/' on their own hostname. Move the
         * login window to our IP so subsequent API requests have the right origin. */
        return captive_handler(req);
    }
    ESP_LOGI(TAG, "Serving local Wi-Fi setup page");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, setup_html_start, setup_html_end - setup_html_start - 1);
}

static esp_err_t captive_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Captive HTTP probe: %s", req->uri);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    /* A non-empty response is necessary for iOS captive-network detection. */
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, "<!doctype html><html><body><a href=\"http://192.168.4.1/\">Set up Arixware Wi-Fi</a></body></html>");
}

static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "arixware");
    cJSON_AddStringToObject(root, "fw_version", "0.1.0-mvp");
    cJSON_AddNumberToObject(root, "uptime_s", esp_timer_get_time() / 1000000);
    net_wifi_status_t status;
    net_wifi_get_status(&status);
    static const char *states[] = { "starting", "setup", "connecting", "connected", "disconnected" };
    cJSON_AddStringToObject(root, "state", states[status.state]);
    cJSON_AddStringToObject(root, "ssid", status.ssid);
    cJSON_AddStringToObject(root, "ip", status.ip);
    cJSON_AddStringToObject(root, "error", status.error);
    cJSON_AddBoolToObject(root, "setup_active", status.setup_active);
    cJSON_AddBoolToObject(root, "setup_complete", status.setup_complete);
    return send_json(req, root);
}

static esp_err_t profile_handler(httpd_req_t *req)
{
    if (!setup_client(req)) return ESP_OK;
    char owner[NET_PROFILE_OWNER_MAX + 1];
    esp_err_t err = net_profile_get_owner(owner, sizeof(owner));
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not read the saved owner name");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "owner_name", owner);
    return send_json(req, root);
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    if (!setup_client(req)) return ESP_OK;
    if (req->method == HTTP_POST && !net_wifi_start_scan()) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Scan is busy. Wait for the current operation, then retry.");
    }
    net_wifi_scan_t scan;
    net_wifi_get_scan(&scan);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "scanning", scan.scanning);
    cJSON_AddStringToObject(root, "error", scan.error);
    cJSON *networks = cJSON_AddArrayToObject(root, "networks");
    for (int i = 0; i < scan.count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", scan.networks[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", scan.networks[i].rssi);
        cJSON_AddBoolToObject(item, "secured", scan.networks[i].secured);
        cJSON_AddItemToArray(networks, item);
    }
    return send_json(req, root);
}

static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    if (!setup_client(req)) return ESP_OK;
    char content_type[64];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/json")) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected application/json");
    }
    char body[1024];
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
    }
    size_t used = 0;
    int timeouts = 0;
    while (used < req->content_len) {
        int ret = httpd_req_recv(req, body + used, req->content_len - used);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 2) continue;
        if (ret <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete request");
        used += ret;
    }
    body[used] = '\0';
    cJSON *root = cJSON_ParseWithLengthOpts(body, used + 1, NULL, true);
    const cJSON *ssid = root ? cJSON_GetObjectItemCaseSensitive(root, "ssid") : NULL;
    const cJSON *pass = root ? cJSON_GetObjectItemCaseSensitive(root, "pass") : NULL;
    if (!cJSON_IsString(ssid) || !cJSON_IsString(pass) || strstr(body, "\\u0000") ||
        !ssid->valuestring[0] || strlen(ssid->valuestring) > 32 || strlen(pass->valuestring) > 63 ||
        (pass->valuestring[0] && strlen(pass->valuestring) < 8)) {
        cJSON_Delete(root);
        memset(body, 0, sizeof(body));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Use a network name up to 32 bytes and a password of 8-63 bytes (or blank for an open network).");
    }
    const cJSON *owner = cJSON_GetObjectItemCaseSensitive(root, "owner_name");
    char owner_name[NET_PROFILE_OWNER_MAX + 1];
    if (!cJSON_IsString(owner) || !net_profile_normalize_owner(owner->valuestring, owner_name)) {
        cJSON_Delete(root);
        memset(body, 0, sizeof(body));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Enter your name. If it is too long, please shorten it.");
    }
    bool queued = net_wifi_connect(ssid->valuestring, pass->valuestring, owner_name);
    memset(pass->valuestring, 0, strlen(pass->valuestring));
    cJSON_Delete(root);
    memset(body, 0, sizeof(body));
    if (!queued) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Connection is busy. Wait and retry.");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

void net_http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.stack_size = 6144;
    config.max_uri_handlers = 16;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }
    /* Before the wildcard captive route below so the app and its API are not redirected. */
    net_remote_register(server);
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page_handler },
        { .uri = "/api/profile", .method = HTTP_GET, .handler = profile_handler },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_handler },
        { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = scan_handler },
        { .uri = "/api/wifi/scan", .method = HTTP_POST, .handler = scan_handler },
        { .uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post_handler },
        { .uri = "/*", .method = HTTP_GET, .handler = captive_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    }
    net_dns_start();
    ESP_LOGI(TAG, "Wi-Fi setup page ready");
}
