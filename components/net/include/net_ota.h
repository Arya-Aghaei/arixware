#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Over-the-air update from the latest GitHub release. Only the app image is replaced; the
 * sounds, images and assets partitions are written by a USB flash. The application supplies
 * the callbacks so the device can show an update screen. Callbacks run on the update task. */

typedef enum {
    NET_OTA_IDLE,
    NET_OTA_CHECKING,
    NET_OTA_CURRENT,    /* the running firmware is already the newest */
    NET_OTA_INSTALLING,
    NET_OTA_DONE,       /* installed; the device restarts */
    NET_OTA_FAILED,
} net_ota_state_t;

typedef struct {
    net_ota_state_t state;
    int progress; /* 0-100 while installing */
    char latest[32];
    char error[64];
} net_ota_status_t;

typedef struct {
    void (*begin)(const char *version);
    void (*progress)(int percent);
    void (*failed)(const char *reason);
    void (*done)(void); /* called just before the restart */
    /* Optional: true while an automatic install would be rude (alarm ringing, timer running). */
    bool (*busy)(void);
} net_ota_ops_t;

void net_ota_set_ops(const net_ota_ops_t *ops);

/** Confirms the running image after it stayed up long enough, so the bootloader keeps it. */
void net_ota_init(void);

/** Checks the latest release and installs it when newer; progress shows in net_ota_get_status()
 *  and, once an install starts, on the update screen. False if already running or Wi-Fi is down. */
bool net_ota_start(void);

/** Automatic updates (checked once a day and shortly after boot). On by default; saved in flash. */
bool net_ota_get_auto(void);
void net_ota_set_auto(bool enabled);

void net_ota_get_status(net_ota_status_t *status);

/** The running firmware version string. */
const char *net_ota_running_version(void);

#ifdef __cplusplus
}
#endif
