#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NET_WIFI_STARTING,
    NET_WIFI_SETUP,
    NET_WIFI_CONNECTING,
    NET_WIFI_CONNECTED,
    NET_WIFI_DISCONNECTED,
} net_wifi_state_t;

typedef struct {
    net_wifi_state_t state;
    char ssid[33];
    char ip[16];
    bool time_synced;
    bool setup_active;
    bool setup_client_ready; /* A phone received an address on the setup hotspot. */
    bool setup_complete; /* A network submitted during this session was saved. */
    char setup_ssid[33];
    char setup_password[13];
    char error[96];
} net_wifi_status_t;

#define NET_WIFI_SCAN_MAX 16
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secured;
} net_wifi_network_t;

typedef struct {
    bool scanning;
    int count;
    net_wifi_network_t networks[NET_WIFI_SCAN_MAX];
    char error[64];
} net_wifi_scan_t;

/** Snapshot copies, safe before initialization completes. */
void net_wifi_get_status(net_wifi_status_t *status);
void net_wifi_get_scan(net_wifi_scan_t *scan);
/** Initializes Wi-Fi and starts a worker. Saved credentials connect automatically. */
void net_wifi_init(void);
/** Queue setup, scan or connection work. False means unavailable/busy/invalid input. */
bool net_wifi_start_setup(void);
bool net_wifi_start_scan(void);
/** Setup must be active. Credentials and the owner name are saved only after obtaining an IP address. */
bool net_wifi_connect(const char *ssid, const char *pass, const char *owner_name);

#ifdef __cplusplus
}
#endif
