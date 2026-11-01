#include "settings.h"
#include "ui_core.h"
#include "ui_tools.h"
#include "net_remote.h"
#include "audio.h"
#include "bsp/esp-bsp.h"
#include "nvs.h"
#include "esp_log.h"
#include "power_button.h"
#include "mmap_generate_sounds.h"

static const char *TAG = "settings";
static int s_brightness;
static bool s_dnd;
static bool s_standby;

static void save_value(const char *key, int value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("settings", NVS_READWRITE, &handle);
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

static int get_brightness(void)
{
    return s_brightness;
}

static void set_brightness(int value, bool commit)
{
    value = value < 5 ? 5 : (value > 100 ? 100 : value);
    if (bsp_display_brightness_set(value) == ESP_OK) {
        s_brightness = value;
        if (commit) {
            save_value("brightness", value);
        }
    }
}

static void set_volume(int value, bool commit)
{
    audio_set_volume(value);
    if (commit) {
        save_value("volume", audio_get_volume());
    }
}

static bool get_dnd(void)
{
    return s_dnd;
}

static void set_dnd(bool enabled)
{
    s_dnd = enabled;
    audio_set_muted(enabled);
    save_value("dnd", enabled);
}

void settings_set_standby(bool standby)
{
    s_standby = standby;
    power_button_set_standby(standby);
    ui_core_set_standby(standby);
    if (standby) {
        bsp_display_backlight_off();
        audio_play_index(MMAP_SOUNDS_POWER_OFF_WAV);
    } else {
        settings_restore_brightness();
        audio_play_index(MMAP_SOUNDS_POWER_ON_WAV);
    }
}

static void request_sleep(void)
{
    settings_set_standby(true);
}

static void request_wake(void)
{
    if (s_standby) {
        settings_set_standby(false);
    }
}

void settings_restore_brightness(void)
{
    bsp_display_brightness_set(s_brightness);
}

/* ---- Phone remote (see net_remote.h). The HTTP task takes the display lock for every UI call. */

static void remote_get_state(net_remote_state_t *state)
{
    bsp_display_lock(-1);
    state->brightness = s_brightness;
    state->volume = audio_get_volume();
    state->dnd = s_dnd;
    state->asleep = s_standby;
    state->mood = ui_core_get_expression();
    state->alarm.hour = ui_alarm_hour();
    state->alarm.minute = ui_alarm_minute();
    state->alarm.enabled = ui_alarm_state() != UI_ALARM_OFF;
    state->alarm.ringing = ui_alarm_state() == UI_ALARM_RINGING;
    state->timer.state = (int)ui_timer_state();
    state->timer.remaining_s = (int)((ui_timer_remaining_ms() + 999) / 1000);
    state->timer.total_s = (int)(ui_timer_total_ms() / 1000);
    bsp_display_unlock();
}

static void remote_set_mood(int expression)
{
    bsp_display_lock(-1);
    ui_core_remote_mood(expression);
    bsp_display_unlock();
}

static void remote_set_brightness(int percent)
{
    bsp_display_lock(-1);
    set_brightness(percent, true);
    bsp_display_unlock();
}

static void remote_set_volume(int percent)
{
    bsp_display_lock(-1);
    set_volume(percent, true);
    bsp_display_unlock();
}

static void remote_set_dnd(bool enabled)
{
    bsp_display_lock(-1);
    set_dnd(enabled);
    bsp_display_unlock();
}

static void remote_set_alarm(int hour, int minute, bool enabled)
{
    bsp_display_lock(-1);
    ui_alarm_set(hour, minute, enabled);
    bsp_display_unlock();
}

static void remote_set_timer(int minutes, bool start)
{
    bsp_display_lock(-1);
    ui_timer_set(minutes, start);
    bsp_display_unlock();
}

static void remote_wake(void)
{
    bsp_display_lock(-1);
    if (s_standby) {
        settings_set_standby(false);
    }
    bsp_display_unlock();
}

static void remote_sleep(void)
{
    bsp_display_lock(-1);
    settings_set_standby(true);
    bsp_display_unlock();
}

void settings_init(void)
{
    s_brightness = bsp_display_brightness_get();
    if (s_brightness < 5 || s_brightness > 100) {
        s_brightness = 100;
    }
    s_dnd = false;
    nvs_handle_t handle;
    if (nvs_open("settings", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t value;
        if (nvs_get_u8(handle, "brightness", &value) == ESP_OK && value >= 5 && value <= 100) {
            set_brightness(value, false);
        }
        if (nvs_get_u8(handle, "volume", &value) == ESP_OK && value <= 100) {
            set_volume(value, false);
        }
        if (nvs_get_u8(handle, "dnd", &value) == ESP_OK && value <= 1) {
            s_dnd = value;
        }
        nvs_close(handle);
    }
    audio_set_muted(s_dnd);
    const ui_settings_controls_t controls = {
        .get_brightness = get_brightness,
        .get_volume = audio_get_volume,
        .set_brightness = set_brightness,
        .set_volume = set_volume,
        .get_dnd = get_dnd,
        .set_dnd = set_dnd,
        .sleep = request_sleep,
        .wake = request_wake,
    };
    ui_core_configure_settings(&controls);
    const net_remote_ops_t remote = {
        .get_state = remote_get_state,
        .set_mood = remote_set_mood,
        .set_brightness = remote_set_brightness,
        .set_volume = remote_set_volume,
        .set_dnd = remote_set_dnd,
        .set_alarm = remote_set_alarm,
        .set_timer = remote_set_timer,
        .sleep = remote_sleep,
        .wake = remote_wake,
    };
    net_remote_set_ops(&remote);
}
