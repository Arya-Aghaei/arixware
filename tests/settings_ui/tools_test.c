/* Timer and alarm engine: real LVGL tick, fake clock, NVS and audio. */
#include "lvgl.h"
#include "ui_tools.h"
#include "nvs.h"
#include <assert.h>
#include <string.h>
#include <time.h>

static time_t s_now;
static uint8_t s_store[3];
static int s_sounds;
static int s_shown = -1;

time_t time(time_t *out)
{
    if (out) {
        *out = s_now;
    }
    return s_now;
}
static int s_playing;
void audio_play_index(int index) { (void)index; s_sounds++; s_playing = 1; }
bool audio_is_playing(void) { return s_playing; }
void audio_stop(void) { s_playing = 0; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) { (void)name; (void)mode; *handle = 0; return ESP_OK; }
static int key_index(const char *key) { return !strcmp(key, "alarm_h") ? 0 : !strcmp(key, "alarm_m") ? 1 : 2; }
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value) { (void)handle; *value = s_store[key_index(key)]; return ESP_OK; }
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value) { (void)handle; s_store[key_index(key)] = value; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle; return ESP_OK; }
void nvs_close(nvs_handle_t handle) { (void)handle; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "err"; }

static void show(ui_tools_screen_t screen) { s_shown = screen; }

static void advance(int ms)
{
    for (int i = 0; i < ms; i += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
}

int main(void)
{
    lv_init();
    s_store[0] = 6;
    s_store[1] = 30;
    s_store[2] = 1;
    struct tm tm_start = { .tm_year = 126, .tm_mon = 0, .tm_mday = 5, .tm_hour = 6, .tm_min = 29, .tm_sec = 50, .tm_isdst = -1 };
    s_now = mktime(&tm_start);
    ui_tools_init(show);

    /* Saved alarm is restored and armed. */
    assert(ui_alarm_state() == UI_ALARM_ARMED && ui_alarm_hour() == 6 && ui_alarm_minute() == 30);
    assert(ui_alarm_seconds_until() == 10);

    /* Fires at the set minute, once, and asks for the alarm screen. */
    s_now += 10;
    advance(300);
    assert(ui_alarm_state() == UI_ALARM_RINGING && s_shown == UI_TOOLS_SCREEN_ALARM && s_sounds == 1);

    /* Snooze silences it and rings again after 9 minutes. */
    ui_alarm_snooze();
    assert(ui_alarm_state() == UI_ALARM_SNOOZED && ui_alarm_seconds_until() == 540);
    s_now += 539;
    advance(300);
    assert(ui_alarm_state() == UI_ALARM_SNOOZED);
    s_now += 1;
    advance(300);
    assert(ui_alarm_state() == UI_ALARM_RINGING);

    /* Wake up keeps it armed for tomorrow and does not refire this minute. */
    ui_alarm_wake_up();
    advance(300);
    assert(ui_alarm_state() == UI_ALARM_ARMED && ui_alarm_seconds_until() > 80000);

    /* An ignored ring snoozes itself after a minute. */
    s_now += 86400 - 540 - 60;
    advance(300);
    for (int i = 0; i < 70 && ui_alarm_state() == UI_ALARM_ARMED; i++) { s_now += 1; advance(1000); }
    assert(ui_alarm_state() == UI_ALARM_RINGING);
    advance(61000);
    assert(ui_alarm_state() == UI_ALARM_SNOOZED);

    /* Editing wraps and persists; disabling stops everything. */
    ui_alarm_adjust(-7, -31);
    assert(ui_alarm_hour() == 23 && ui_alarm_minute() == 59 && s_store[0] == 23 && s_store[1] == 59);
    ui_alarm_set_enabled(false);
    assert(ui_alarm_state() == UI_ALARM_OFF && ui_alarm_seconds_until() == -1 && s_store[2] == 0);

    /* Timer: clamp, run, pause, resume, ring, dismiss. */
    ui_timer_adjust(-10);
    assert(ui_timer_total_ms() == 60000);
    ui_timer_adjust(200);
    assert(ui_timer_total_ms() == 99 * 60000);
    ui_timer_adjust(-98);
    ui_timer_toggle();
    advance(500);
    assert(ui_timer_state() == UI_TIMER_RUNNING && ui_timer_remaining_ms() < 60000 - 400);
    ui_timer_toggle();
    uint32_t frozen = ui_timer_remaining_ms();
    advance(1000);
    assert(ui_timer_state() == UI_TIMER_PAUSED && ui_timer_remaining_ms() == frozen);
    ui_timer_adjust(5); /* ignored unless idle */
    assert(ui_timer_total_ms() == 60000);
    ui_timer_toggle();
    advance(61000);
    assert(ui_timer_state() == UI_TIMER_RINGING && s_shown == UI_TOOLS_SCREEN_TIMER);
    int sounds = s_sounds;
    advance(4500);
    assert(s_sounds == sounds); /* clip still "playing": no overlap */
    s_playing = 0;
    advance(1500);
    assert(s_sounds > sounds);
    ui_timer_toggle();
    assert(ui_timer_state() == UI_TIMER_IDLE && ui_timer_remaining_ms() == 60000);
    return 0;
}
