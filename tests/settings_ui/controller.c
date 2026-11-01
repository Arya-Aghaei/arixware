/* Exercise the real settings controller with simulated storage and hardware. */
#include "settings.h"
#include "ui_core.h"
#include "audio.h"
#include "bsp/esp-bsp.h"
#include "nvs.h"
#include "power_button.h"
#include "ui_tools.h"
#include "net_remote.h"
#include "iot_button.h"
#include "button_gpio.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static ui_settings_controls_t s_controls;
static int s_display = 100;
static int s_volume = 70;
static int s_saved_brightness = -1;
static int s_saved_volume = -1;
static int s_pending_brightness = -1;
static int s_pending_volume = -1;
static int s_saved_dnd = -1;
static int s_pending_dnd = -1;
static bool s_muted;
static bool s_ui_standby;
static void (*s_physical_click)(void *, void *);
static int s_commits;
static bool s_storage_failure;
static bool s_display_failure;

void ui_core_configure_settings(const ui_settings_controls_t *controls) { s_controls = *controls; }
static net_remote_ops_t s_remote;
static int s_mood = -1;
static int s_alarm_hour_set = -1;
static int s_timer_minutes_set = -1;
static int s_locks;
void net_remote_set_ops(const net_remote_ops_t *ops) { s_remote = *ops; }
esp_err_t bsp_display_lock(int timeout_ms) { (void)timeout_ms; s_locks++; return ESP_OK; }
void bsp_display_unlock(void) { s_locks--; }
void ui_core_remote_mood(int expression) { s_mood = expression; }
int ui_core_get_expression(void) { return s_mood; }
void ui_alarm_set(int hour, int minute, bool enabled) { (void)minute; (void)enabled; s_alarm_hour_set = hour; }
void ui_timer_set(int minutes, bool start) { (void)start; s_timer_minutes_set = minutes; }
int ui_alarm_hour(void) { return 7; }
int ui_alarm_minute(void) { return 30; }
ui_alarm_state_t ui_alarm_state(void) { return UI_ALARM_ARMED; }
ui_timer_state_t ui_timer_state(void) { return UI_TIMER_IDLE; }
uint32_t ui_timer_remaining_ms(void) { return 300000; }
uint32_t ui_timer_total_ms(void) { return 300000; }
int bsp_display_brightness_get(void) { return s_display; }
esp_err_t bsp_display_brightness_set(int value)
{
    if (s_display_failure) {
        return ESP_FAIL;
    }
    s_display = value;
    return ESP_OK;
}
void audio_set_volume(int value) { s_volume = value < 0 ? 0 : (value > 100 ? 100 : value); }
int audio_get_volume(void) { return s_volume; }
void audio_set_muted(bool muted) { s_muted = muted; }
void audio_play_index(int index) { (void)index; }
void ui_core_set_standby(bool standby) { s_ui_standby = standby; }
esp_err_t bsp_display_backlight_off(void) { s_display = 0; return ESP_OK; }
esp_err_t iot_button_new_gpio_device(const button_config_t *config,
                                     const button_gpio_config_t *gpio, button_handle_t *handle)
{
    (void)config;
    (void)gpio;
    *handle = (void *)1;
    return ESP_OK;
}
esp_err_t iot_button_register_cb(button_handle_t handle, int event, void *args,
                                void (*callback)(void *, void *), void *data)
{
    (void)handle;
    (void)event;
    (void)args;
    (void)data;
    s_physical_click = callback;
    return ESP_OK;
}
const char *esp_err_to_name(esp_err_t err) { return err == ESP_OK ? "OK" : "Simulated failure"; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    (void)mode;
    assert(strcmp(name, "settings") == 0);
    *handle = 1;
    return s_storage_failure ? ESP_FAIL : ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    (void)handle;
    int saved = strcmp(key, "brightness") == 0 ? s_saved_brightness :
                strcmp(key, "dnd") == 0 ? s_saved_dnd : s_saved_volume;
    if (saved < 0) {
        return ESP_FAIL;
    }
    *value = saved;
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    (void)handle;
    if (strcmp(key, "brightness") == 0) {
        s_pending_brightness = value;
    } else if (strcmp(key, "dnd") == 0) {
        s_pending_dnd = value;
    } else {
        assert(strcmp(key, "volume") == 0);
        s_pending_volume = value;
    }
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    if (s_pending_brightness >= 0) {
        s_saved_brightness = s_pending_brightness;
    }
    if (s_pending_volume >= 0) {
        s_saved_volume = s_pending_volume;
    }
    if (s_pending_dnd >= 0) {
        s_saved_dnd = s_pending_dnd;
    }
    s_pending_dnd = -1;
    s_pending_brightness = -1;
    s_pending_volume = -1;
    s_commits++;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }

int main(void)
{
    settings_init();
    assert(s_controls.get_brightness() == 100 && s_controls.get_volume() == 70);
    assert(s_commits == 0);

    /* Phone remote: every call takes and releases the display lock, and changes are persisted. */
    net_remote_state_t remote_state;
    s_remote.set_brightness(40);
    assert(s_display == 40 && s_locks == 0 && s_commits == 1);
    s_remote.set_volume(25);
    s_remote.set_dnd(true);
    s_remote.get_state(&remote_state);
    assert(remote_state.brightness == 40 && remote_state.volume == 25 && remote_state.dnd && s_muted);
    assert(remote_state.alarm.hour == 7 && remote_state.alarm.enabled && remote_state.timer.remaining_s == 300);
    s_remote.set_mood(14);
    s_remote.get_state(&remote_state);
    assert(remote_state.mood == 14 && !remote_state.asleep);
    s_remote.set_alarm(6, 45, true);
    s_remote.set_timer(12, true);
    assert(s_mood == 14 && s_alarm_hour_set == 6 && s_timer_minutes_set == 12 && s_locks == 0);
    /* Sleeping from the phone blanks the display; waking from the phone brings it back. */
    s_remote.sleep();
    s_remote.get_state(&remote_state);
    assert(remote_state.asleep && s_ui_standby && s_display == 0);
    s_remote.wake();
    s_remote.get_state(&remote_state);
    assert(!remote_state.asleep && !s_ui_standby && s_display > 0);
    s_remote.set_dnd(false);
    s_remote.set_volume(70);
    s_remote.set_brightness(100);
    s_commits = 0;
    s_saved_brightness = s_saved_volume = s_saved_dnd = -1;
    s_pending_brightness = s_pending_volume = s_pending_dnd = -1;

    assert(!s_controls.get_dnd() && !s_muted);
    s_controls.set_brightness(35, false);
    s_controls.set_volume(20, false);
    assert(s_display == 35 && s_volume == 20 && s_commits == 0);
    s_controls.set_brightness(35, true);
    s_controls.set_volume(20, true);
    assert(s_saved_brightness == 35 && s_saved_volume == 20 && s_commits == 2);
    s_display = 100;
    s_volume = 70;
    settings_init();
    assert(s_display == 35 && s_volume == 20);
    s_display = 0;
    settings_restore_brightness();
    assert(s_display == 35);
    s_controls.set_brightness(-10, true);
    s_controls.set_volume(-10, true);
    assert(s_display == 5 && s_volume == 0);
    s_display = 100;
    s_volume = 70;
    settings_init();
    assert(s_display == 5 && s_volume == 0);
    s_controls.set_brightness(150, true);
    s_controls.set_volume(150, true);
    assert(s_display == 100 && s_volume == 100);
    s_display_failure = true;
    s_controls.set_brightness(40, true);
    assert(s_controls.get_brightness() == 100 && s_saved_brightness == 100);
    s_display_failure = false;
    s_storage_failure = true;
    s_controls.set_brightness(45, true);
    assert(s_display == 45 && s_saved_brightness == 100);
    s_storage_failure = false;
    s_saved_brightness = 1;
    s_saved_volume = 255;
    s_display = 100;
    s_volume = 70;
    settings_init();
    assert(s_display == 100 && s_volume == 70);
    s_controls.set_dnd(true);
    assert(s_controls.get_dnd() && s_muted && s_saved_dnd == 1);
    s_controls.set_volume(55, true);
    assert(s_muted && s_volume == 55);
    s_muted = false;
    settings_init();
    assert(s_controls.get_dnd() && s_muted && s_volume == 55);
    s_controls.set_dnd(false);
    assert(!s_muted && s_volume == 55 && s_saved_dnd == 0);
    power_button_init(settings_set_standby);
    s_controls.set_brightness(35, true);
    s_controls.sleep();
    assert(s_ui_standby && s_display == 0);
    assert(s_physical_click);
    s_physical_click(NULL, NULL); /* First physical press after UI sleep must wake. */
    assert(!s_ui_standby && s_display == 35);
    s_physical_click(NULL, NULL);
    assert(s_ui_standby && s_display == 0);
    s_physical_click(NULL, NULL);
    assert(!s_ui_standby && s_display == 35);
    puts("Settings controller: defaults, ranges, persistence, standby and failures passed");
    return 0;
}
