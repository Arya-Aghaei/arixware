#include "ui_tools.h"
#include "audio.h"
#include "mmap_generate_sounds.h"
#include "lvgl.h"
#include "nvs.h"
#include "esp_log.h"
#include <time.h>

#define TICK_MS 250
#define RING_SOUND_TICKS 4         /* retry the ring sound each second; clips last 8-13 s */
#define TIMER_RING_TICKS (120 * 4) /* a forgotten timer gives up after 2 min */
#define ALARM_RING_TICKS (60 * 4)  /* an ignored alarm snoozes itself after 1 min */
#define SNOOZE_SECONDS (9 * 60)
#define TIMER_DEFAULT_MIN 5
#define TIMER_MAX_MIN 99

static const char *TAG = "ui_tools";

static void (*s_show)(ui_tools_screen_t);
static lv_timer_t *s_tick;

static ui_timer_state_t s_timer_state;
static uint32_t s_timer_total_ms = TIMER_DEFAULT_MIN * 60000;
static uint32_t s_timer_deadline;     /* lv_tick_get() value while running */
static uint32_t s_timer_left_ms;      /* frozen remainder while paused */
static int s_timer_ring_ticks;

static ui_alarm_state_t s_alarm_state;
static int s_alarm_hour = 7;
static int s_alarm_minute;
static bool s_alarm_enabled;
static time_t s_snooze_until;
static time_t s_last_fired_minute = -1;
static int s_alarm_ring_ticks;

static void save_u8(const char *key, uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("tools", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, key, value);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not save %s: %s", key, esp_err_to_name(err));
    }
}

static bool clock_synced(struct tm *out, time_t *now)
{
    *now = time(NULL);
    localtime_r(now, out);
    return out->tm_year + 1900 >= 2020;
}

static uint32_t timer_left(void)
{
    if (s_timer_state == UI_TIMER_RUNNING) {
        int32_t left = (int32_t)(s_timer_deadline - lv_tick_get());
        return left > 0 ? (uint32_t)left : 0;
    }
    if (s_timer_state == UI_TIMER_PAUSED) {
        return s_timer_left_ms;
    }
    return s_timer_state == UI_TIMER_RINGING ? 0 : s_timer_total_ms;
}

ui_timer_state_t ui_timer_state(void) { return s_timer_state; }
uint32_t ui_timer_remaining_ms(void) { return timer_left(); }
uint32_t ui_timer_total_ms(void) { return s_timer_total_ms; }

void ui_timer_adjust(int minutes)
{
    if (s_timer_state != UI_TIMER_IDLE) {
        return;
    }
    int now_min = (int)(s_timer_total_ms / 60000) + minutes;
    now_min = now_min < 1 ? 1 : (now_min > TIMER_MAX_MIN ? TIMER_MAX_MIN : now_min);
    s_timer_total_ms = (uint32_t)now_min * 60000;
}

void ui_timer_set(int minutes, bool start)
{
    if (s_timer_state != UI_TIMER_IDLE) {
        return;
    }
    minutes = minutes < 1 ? 1 : (minutes > TIMER_MAX_MIN ? TIMER_MAX_MIN : minutes);
    s_timer_total_ms = (uint32_t)minutes * 60000;
    if (start) {
        ui_timer_toggle();
    }
}

void ui_timer_toggle(void)
{
    switch (s_timer_state) {
    case UI_TIMER_IDLE:
        s_timer_deadline = lv_tick_get() + s_timer_total_ms;
        s_timer_state = UI_TIMER_RUNNING;
        break;
    case UI_TIMER_RUNNING:
        s_timer_left_ms = timer_left();
        s_timer_state = UI_TIMER_PAUSED;
        break;
    case UI_TIMER_PAUSED:
        s_timer_deadline = lv_tick_get() + s_timer_left_ms;
        s_timer_state = UI_TIMER_RUNNING;
        break;
    case UI_TIMER_RINGING:
        s_timer_state = UI_TIMER_IDLE;
        audio_stop();
        break;
    }
}

void ui_timer_reset(void)
{
    if (s_timer_state == UI_TIMER_RINGING) {
        audio_stop();
    }
    s_timer_state = UI_TIMER_IDLE;
}

void ui_alarm_set_enabled(bool enabled);
ui_alarm_state_t ui_alarm_state(void) { return s_alarm_state; }
int ui_alarm_hour(void) { return s_alarm_hour; }
int ui_alarm_minute(void) { return s_alarm_minute; }

void ui_alarm_adjust(int hour_delta, int minute_delta)
{
    s_alarm_hour = ((s_alarm_hour + hour_delta) % 24 + 24) % 24;
    s_alarm_minute = ((s_alarm_minute + minute_delta) % 60 + 60) % 60;
    /* A new time must be able to fire even if the old one already rang this minute. */
    s_last_fired_minute = -1;
    if (s_alarm_state == UI_ALARM_SNOOZED) {
        s_alarm_state = UI_ALARM_ARMED;
    }
    save_u8("alarm_h", (uint8_t)s_alarm_hour);
    save_u8("alarm_m", (uint8_t)s_alarm_minute);
}

void ui_alarm_set(int hour, int minute, bool enabled)
{
    s_alarm_hour = hour < 0 ? 0 : (hour > 23 ? 23 : hour);
    s_alarm_minute = minute < 0 ? 0 : (minute > 59 ? 59 : minute);
    s_last_fired_minute = -1;
    save_u8("alarm_h", (uint8_t)s_alarm_hour);
    save_u8("alarm_m", (uint8_t)s_alarm_minute);
    ui_alarm_set_enabled(enabled);
}

void ui_alarm_set_enabled(bool enabled)
{
    if (!enabled && s_alarm_state == UI_ALARM_RINGING) {
        audio_stop();
    }
    s_alarm_enabled = enabled;
    s_alarm_state = enabled ? UI_ALARM_ARMED : UI_ALARM_OFF;
    save_u8("alarm_on", enabled);
}

int ui_alarm_seconds_until(void)
{
    struct tm tm_now;
    time_t now;
    if (s_alarm_state == UI_ALARM_OFF || s_alarm_state == UI_ALARM_RINGING ||
        !clock_synced(&tm_now, &now)) {
        return -1;
    }
    if (s_alarm_state == UI_ALARM_SNOOZED) {
        return s_snooze_until > now ? (int)(s_snooze_until - now) : 0;
    }
    int target = s_alarm_hour * 3600 + s_alarm_minute * 60;
    int current = tm_now.tm_hour * 3600 + tm_now.tm_min * 60 + tm_now.tm_sec;
    int diff = target - current;
    return diff > 0 ? diff : diff + 86400;
}

void ui_alarm_snooze(void)
{
    if (s_alarm_state != UI_ALARM_RINGING) {
        return;
    }
    s_snooze_until = time(NULL) + SNOOZE_SECONDS;
    s_alarm_state = UI_ALARM_SNOOZED;
    audio_stop();
}

void ui_alarm_wake_up(void)
{
    if (s_alarm_state == UI_ALARM_RINGING) {
        s_alarm_state = UI_ALARM_ARMED;
        audio_stop();
    }
}

static void start_alarm_ring(void)
{
    s_alarm_state = UI_ALARM_RINGING;
    s_alarm_ring_ticks = 0;
    if (s_show) {
        s_show(UI_TOOLS_SCREEN_ALARM);
    }
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_timer_state == UI_TIMER_RUNNING && timer_left() == 0) {
        s_timer_state = UI_TIMER_RINGING;
        s_timer_ring_ticks = 0;
        if (s_show) {
            s_show(UI_TOOLS_SCREEN_TIMER);
        }
    }
    if (s_timer_state == UI_TIMER_RINGING) {
        if (s_timer_ring_ticks % RING_SOUND_TICKS == 0 && !audio_is_playing()) {
            audio_play_index(MMAP_SOUNDS_ALARM_TIMER_WAV);
        }
        if (++s_timer_ring_ticks >= TIMER_RING_TICKS) {
            s_timer_state = UI_TIMER_IDLE;
            audio_stop();
        }
    }

    struct tm tm_now;
    time_t now;
    if ((s_alarm_state == UI_ALARM_ARMED || s_alarm_state == UI_ALARM_SNOOZED) &&
        clock_synced(&tm_now, &now)) {
        if (s_alarm_state == UI_ALARM_SNOOZED) {
            if (now >= s_snooze_until) {
                start_alarm_ring();
            }
        } else if (tm_now.tm_hour == s_alarm_hour && tm_now.tm_min == s_alarm_minute &&
                   now / 60 != s_last_fired_minute) {
            s_last_fired_minute = now / 60;
            start_alarm_ring();
        }
    }
    if (s_alarm_state == UI_ALARM_RINGING) {
        if (s_alarm_ring_ticks % RING_SOUND_TICKS == 0 && !audio_is_playing()) {
            audio_play_index(MMAP_SOUNDS_ALARM_WAKE_UP_WAV);
        }
        if (++s_alarm_ring_ticks >= ALARM_RING_TICKS) {
            ui_alarm_snooze();
        }
    }
}

void ui_tools_init(void (*show)(ui_tools_screen_t screen))
{
    s_show = show;
    nvs_handle_t handle;
    if (nvs_open("tools", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t value;
        if (nvs_get_u8(handle, "alarm_h", &value) == ESP_OK && value < 24) {
            s_alarm_hour = value;
        }
        if (nvs_get_u8(handle, "alarm_m", &value) == ESP_OK && value < 60) {
            s_alarm_minute = value;
        }
        if (nvs_get_u8(handle, "alarm_on", &value) == ESP_OK && value <= 1) {
            s_alarm_enabled = value;
        }
        nvs_close(handle);
    }
    s_alarm_state = s_alarm_enabled ? UI_ALARM_ARMED : UI_ALARM_OFF;
    if (!s_tick) {
        s_tick = lv_timer_create(tick_cb, TICK_MS, NULL);
    }
}
