#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Phone remote over the home Wi-Fi: a small web app (installable to the home screen on iOS and
 * Android) talks to JSON endpoints served by the arix. Every request needs the 6-digit remote
 * code shown in Settings > About, which the app asks for once and remembers. */

typedef struct {
    int brightness;
    int volume;
    bool dnd;
    bool asleep;
    int mood; /* the eyes' current mood, -1 if the eyes are not showing */
    struct {
        int hour;
        int minute;
        bool enabled;
        bool ringing;
    } alarm;
    struct {
        int state;        /* 0 idle, 1 running, 2 paused, 3 ringing */
        int remaining_s;
        int total_s;
    } timer;
} net_remote_state_t;

/* Implemented by the application, which owns the UI lock. Called from the HTTP task. */
typedef struct {
    void (*get_state)(net_remote_state_t *state);
    void (*set_mood)(int expression);
    void (*set_brightness)(int percent);
    void (*set_volume)(int percent);
    void (*set_dnd)(bool enabled);
    void (*set_alarm)(int hour, int minute, bool enabled);
    void (*set_timer)(int minutes, bool start);
    void (*sleep)(void);
    void (*wake)(void);
} net_remote_ops_t;

void net_remote_set_ops(const net_remote_ops_t *ops);

/** The remote code (6 digits, created on first use and kept in flash). `out` needs 7 bytes. */
void net_remote_code(char out[7]);

/** Adds the app and API routes to an esp_http_server handle; call before any wildcard route. */
void net_remote_register(void *httpd_handle);

#ifdef __cplusplus
}
#endif
