#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Timer and wake-up alarm engine. It keeps running while other screens are shown, so a ring
 * can fire from anywhere; the screens only render and control this state. */

typedef enum {
    UI_TOOLS_SCREEN_TIMER,
    UI_TOOLS_SCREEN_ALARM,
} ui_tools_screen_t;

typedef enum {
    UI_TIMER_IDLE,
    UI_TIMER_RUNNING,
    UI_TIMER_PAUSED,
    UI_TIMER_RINGING,
} ui_timer_state_t;

typedef enum {
    UI_ALARM_OFF,
    UI_ALARM_ARMED,
    UI_ALARM_RINGING,
    UI_ALARM_SNOOZED,
} ui_alarm_state_t;

/** Loads the saved alarm and starts the 250 ms engine tick. `show` is called when a timer or
 * alarm starts ringing so the UI can bring the matching screen forward. Needs the LVGL lock. */
void ui_tools_init(void (*show)(ui_tools_screen_t screen));

ui_timer_state_t ui_timer_state(void);
/** Remaining time while running/paused; the configured length while idle; 0 while ringing. */
uint32_t ui_timer_remaining_ms(void);
/** Length the running timer started from (for progress), or the idle setting. */
uint32_t ui_timer_total_ms(void);
/** Idle only: change the length by whole minutes (clamped 1..99). */
void ui_timer_adjust(int minutes);
/** Idle -> running, running <-> paused, ringing -> dismissed. */
void ui_timer_toggle(void);
/** Sets the length (1..99 min) and, when `start`, starts it; ignored unless idle. */
void ui_timer_set(int minutes, bool start);
/** Stops and returns to idle with the same length; also silences a ringing timer. */
void ui_timer_reset(void);

ui_alarm_state_t ui_alarm_state(void);
int ui_alarm_hour(void);
int ui_alarm_minute(void);
/** Sets the alarm time and on/off state at once; persists. */
void ui_alarm_set(int hour, int minute, bool enabled);
/** Wraps hour (0..23) and minute (0..59); persists. */
void ui_alarm_adjust(int hour_delta, int minute_delta);
void ui_alarm_set_enabled(bool enabled);
/** Seconds until the next ring while armed or snoozed, else -1 (also -1 before time sync). */
int ui_alarm_seconds_until(void);
/** Ringing: silence and ring again after the snooze delay. */
void ui_alarm_snooze(void);
/** Ringing: silence until the same time tomorrow. */
void ui_alarm_wake_up(void);

#ifdef __cplusplus
}
#endif
