#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Starts the local setup page, provisioning/status endpoints and captive DNS. Call after net_wifi_init(). */
void net_http_start(void);

#ifdef __cplusplus
}
#endif
