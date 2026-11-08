#include "ui_core.h"
#include "ui_screen_welcome.h"
#include "ui_screen_eyes.h"
#include "ui_screen_clock.h"
#include "ui_screen_timer.h"
#include "ui_screen_alarm.h"
#include "ui_screen_quote.h"
#include "ui_tools.h"
#include "ui_screen_settings.h"
#include "ui_screen_update.h"
#include "ui_panel.h"
#include "ui_orient.h"
#include "eyes_renderer.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>

#define UI_CORE_TRANSITION_MS 400
#define SETTINGS_OPEN_MS 320
#define SETTINGS_CLOSE_MS 280
#define SCREEN_FADE_MS 320
#define UI_CORE_PREWARM_MS 650

typedef struct {
    lv_obj_t *(*create)(void);
    /* Optional: the screen's panel-ready pixels, so the eyes can cross-fade into it directly. */
    const uint16_t *(*panel_pixels)(void);
} ui_screen_t;

/* Swipeable widget screens, in order; index 0 (eyes) is the "home" screen. */
static const ui_screen_t s_screens[] = {
    { ui_screen_eyes_create, NULL },
    { ui_screen_clock_create, ui_screen_clock_panel_pixels },
    { ui_screen_timer_create, ui_screen_timer_panel_pixels },
    { ui_screen_alarm_create, ui_screen_alarm_panel_pixels },
    { ui_screen_quote_create, ui_screen_quote_panel_pixels },
};
#define UI_SCREEN_COUNT ((int)(sizeof(s_screens) / sizeof(s_screens[0])))

static const eyes_renderer_api_t *s_active_eyes_api;
static int s_screen_index;
static bool s_switching;
static bool s_settings_open;
static bool s_top_edge_press;
static bool s_left_edge_press;
static bool s_standby;
static bool s_updating;
static lv_obj_t *s_update_screen;
static char s_update_version[32];

static ui_settings_controls_t s_settings_controls;

static void gesture_event_cb(lv_event_t *e);

static void register_input(lv_obj_t *scr)
{
    lv_obj_add_event_cb(scr, gesture_event_cb, LV_EVENT_ALL, NULL);
    /* The eyes placeholder is clickable and must forward the initial edge press too. */
    for (uint32_t i = 0; i < lv_obj_get_child_count(scr); i++) {
        lv_obj_add_flag(lv_obj_get_child(scr, i), LV_OBJ_FLAG_EVENT_BUBBLE);
    }
}

static void finish_leaving_eyes(void *scr)
{
    /* Tear down first: an instant load with auto_del deletes the eyes screen (and the renderer's
     * placeholder object) synchronously. The panel already shows this screen's exact pixels, so
     * LVGL's redraw after the hand-back changes nothing visible. */
    if (s_active_eyes_api) {
        s_active_eyes_api->destroy();
        s_active_eyes_api = NULL;
    }
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    s_switching = false;
}

static void eyes_fade_done(void *scr)
{
    /* Called from the renderer's own timer; tear it down outside that callback. */
    lv_async_call(finish_leaving_eyes, scr);
}

static void finish_screen_fade(void *scr)
{
    /* Runs while the fade still owns the panel, which already shows this screen's pixels. */
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    s_switching = false;
}

static void switch_screen_async(void *target);

static void load_screen(int index)
{
    if (ui_panel_fade_active()) {
        /* A screen is mid cross-fade on its own buffers; never delete it underneath. */
        lv_async_call(switch_screen_async, (void *)(intptr_t)index);
        return;
    }
    int previous = s_screen_index;
    bool leaving_eyes = s_screen_index == 0 && index != 0 && s_active_eyes_api;
    lv_obj_t *scr = s_screens[index].create();
    register_input(scr);
    s_screen_index = index;

    if (leaving_eyes) {
        const uint16_t *pixels = s_screens[index].panel_pixels ? s_screens[index].panel_pixels() : NULL;
        if (pixels && s_active_eyes_api->fade_out_to) {
            /* LVGL redraws this panel far too slowly for a smooth full-screen fade (~145 ms per
             * frame), so the eyes renderer cross-fades into the new screen while it still drives
             * the panel directly. */
            s_switching = true;
            s_active_eyes_api->fade_out_to(pixels, UI_CORE_TRANSITION_MS, eyes_fade_done, scr);
            return;
        }
        /* The eyes renderer owns its own buffers and lv_timers, which generic LVGL screen
         * deletion doesn't know about; tear it down explicitly before leaving. */
        s_active_eyes_api->destroy();
        s_active_eyes_api = NULL;
    }
    if (index != 0 && previous != 0 && s_screens[previous].panel_pixels && s_screens[index].panel_pixels) {
        /* Same reason as the eyes cross-fade: an LVGL fade between full screens runs at ~7 fps here. */
        const uint16_t *from = s_screens[previous].panel_pixels();
        const uint16_t *to = s_screens[index].panel_pixels();
        if (from && to) {
            s_switching = true;
            if (ui_panel_fade(from, to, SCREEN_FADE_MS, finish_screen_fade, scr)) {
                return;
            }
        }
    }
    if (index == 0) {
        s_active_eyes_api = eyes_renderer_get_default();
    }
    /* auto_del=true frees the previous screen once the transition completes. The eyes draw
     * straight to the panel and open with their own wake-up animation, so an LVGL fade there
     * would only burn frames rendering into the adapter's bypass buffer. */
    if (index == 0) {
        lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    } else {
        lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, UI_CORE_TRANSITION_MS, 0, true);
    }
}

static void switch_screen_async(void *target)
{
    if (s_updating) {
        s_switching = false;
        return;
    }
    load_screen((int)(intptr_t)target);
}

static void finish_closing_settings(void *scr)
{
    /* Runs while the fade still owns the panel, which already shows the destination's pixels. */
    s_settings_open = false;
    if (scr) {
        lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    } else {
        load_screen(0); /* the eyes open with their own wake-up animation from black */
    }
    s_switching = false;
}

static void close_settings_async(void *arg)
{
    (void)arg;
    ui_screen_settings_freeze();
    const uint16_t *from = ui_screen_settings_panel_pixels();
    if (s_screen_index == 0) {
        if (!from || !ui_panel_fade(from, NULL, SETTINGS_CLOSE_MS, finish_closing_settings, NULL)) {
            finish_closing_settings(NULL);
        }
        return;
    }
    lv_obj_t *scr = s_screens[s_screen_index].create();
    register_input(scr);
    const uint16_t *to = s_screens[s_screen_index].panel_pixels ? s_screens[s_screen_index].panel_pixels() : NULL;
    if (!from || !to || !ui_panel_fade(from, to, SETTINGS_CLOSE_MS, finish_closing_settings, scr)) {
        finish_closing_settings(scr);
    }
}

static void close_settings(void)
{
    if (s_switching || !s_settings_open) {
        return;
    }
    s_switching = true;
    lv_async_call(close_settings_async, NULL);
}

static void finish_opening_settings(void *scr)
{
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    s_switching = false;
}

static void open_settings_async(void *arg)
{
    (void)arg;
    lv_obj_t *scr = ui_screen_settings_create(&s_settings_controls);
    register_input(scr);
    s_settings_open = true;
    const uint16_t *pixels = ui_screen_settings_panel_pixels();
    /* The shade draws straight to the panel, so it arrives as a direct cross-fade: from the eyes
     * via their own renderer, otherwise from the current screen's (or a black) frame. */
    if (pixels && s_active_eyes_api && s_active_eyes_api->fade_out_to) {
        s_active_eyes_api->fade_out_to(pixels, SETTINGS_OPEN_MS, eyes_fade_done, scr);
        return;
    }
    if (s_active_eyes_api) {
        s_active_eyes_api->destroy();
        s_active_eyes_api = NULL;
    }
    const uint16_t *from = s_screens[s_screen_index].panel_pixels ? s_screens[s_screen_index].panel_pixels() : NULL;
    if (!pixels || !ui_panel_fade(from, pixels, SETTINGS_OPEN_MS, finish_opening_settings, scr)) {
        finish_opening_settings(scr);
    }
}

static lv_dir_t visual_direction(lv_dir_t raw)
{
    /* Undo the per-object clockwise content rotation for gesture directions. */
    static const lv_dir_t dirs[] = { LV_DIR_TOP, LV_DIR_RIGHT, LV_DIR_BOTTOM, LV_DIR_LEFT };
    for (int i = 0; i < 4; i++) {
        if (dirs[i] == raw) {
            return dirs[(i + 4 - UI_ROTATION_DEG / 90) % 4];
        }
    }
    return LV_DIR_NONE;
}

static void gesture_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCREEN_LOADED) {
        s_switching = false;
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        int x, y;
        switch (UI_ROTATION_DEG) {
        case 90: x = p.y; y = 465 - p.x; break;
        case 180: x = 465 - p.x; y = 465 - p.y; break;
        case 270: x = 465 - p.y; y = p.x; break;
        default: x = p.x; y = p.y; break;
        }
        s_top_edge_press = y >= 0 && y <= 60;
        s_left_edge_press = x >= 0 && x <= 60;
        return;
    }
    if (code != LV_EVENT_GESTURE || s_switching || ui_panel_fade_active()) {
        return;
    }
    if (s_settings_open && ui_screen_settings_touch_captured()) {
        return;
    }
    lv_dir_t dir = visual_direction(lv_indev_get_gesture_dir(indev));
    if (s_settings_open) {
        if ((dir == LV_DIR_TOP || (dir == LV_DIR_RIGHT && s_left_edge_press)) &&
            !ui_screen_settings_back()) {
            close_settings();
        }
        return;
    }
    if (dir == LV_DIR_BOTTOM && s_top_edge_press && s_settings_controls.get_brightness) {
        s_top_edge_press = false;
        s_switching = true;
        lv_async_call(open_settings_async, NULL);
        return;
    }
    int delta;
    if (dir == LV_DIR_LEFT) {
        delta = 1;
    } else if (dir == LV_DIR_RIGHT) {
        delta = -1;
    } else {
        return;
    }
    /* Never delete a screen while it is dispatching an input event. */
    s_switching = true;
    int target = ((s_screen_index + delta) % UI_SCREEN_COUNT + UI_SCREEN_COUNT) % UI_SCREEN_COUNT;
    lv_async_call(switch_screen_async, (void *)(intptr_t)target);
}

enum { SCREEN_TIMER = 2, SCREEN_ALARM = 3 };

/* A timer or alarm started ringing: bring its screen forward unless the user is busy elsewhere
 * (the sound plays regardless, and the ring state is waiting when they come back). */
static void tools_show_cb(ui_tools_screen_t screen)
{
    int target = screen == UI_TOOLS_SCREEN_TIMER ? SCREEN_TIMER : SCREEN_ALARM;
    if (s_settings_controls.wake) {
        s_settings_controls.wake(); /* an alarm must be seen even if the screen was off */
    }
    if (s_updating || s_settings_open || s_switching || s_screen_index == target) {
        return;
    }
    s_switching = true;
    lv_async_call(switch_screen_async, (void *)(intptr_t)target);
}

static int s_pending_mood = -1;

static void mood_timer_cb(lv_timer_t *timer)
{
    if (s_pending_mood >= 0 && s_screen_index == 0 && !s_switching && s_active_eyes_api) {
        ui_core_set_expression((ui_expression_t)s_pending_mood);
        s_pending_mood = -1;
    }
    if (s_pending_mood < 0) {
        lv_timer_pause(timer);
    }
}

int ui_core_get_expression(void)
{
    if (s_pending_mood >= 0) {
        return s_pending_mood;
    }
    if (s_screen_index == 0 && s_active_eyes_api && s_active_eyes_api->get_expression) {
        return s_active_eyes_api->get_expression();
    }
    return -1;
}

void ui_core_remote_mood(int expression)
{
    static lv_timer_t *timer;
    if (expression < 0 || expression >= UI_EXPRESSION_COUNT) {
        return;
    }
    if (s_updating) {
        return;
    }
    s_pending_mood = expression;
    if (!timer) {
        timer = lv_timer_create(mood_timer_cb, 100, NULL);
    }
    lv_timer_resume(timer);
    /* Moods only exist on the eyes screen; the timer applies it once they are showing. */
    if (s_screen_index != 0 && !s_settings_open && !s_switching) {
        s_switching = true;
        lv_async_call(switch_screen_async, (void *)(intptr_t)0);
    }
    lv_timer_ready(timer);
}

void ui_core_configure_settings(const ui_settings_controls_t *controls)
{
    if (controls && controls->get_brightness && controls->get_volume &&
        controls->set_brightness && controls->set_volume && controls->get_dnd &&
        controls->set_dnd && controls->sleep) {
        s_settings_controls = *controls;
    }
}

void ui_core_init(void)
{
    ui_tools_init(tools_show_cb);
}

static void prewarm_settings_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    if (s_settings_controls.get_brightness) {
        ui_screen_settings_prewarm();
    }
}

static void welcome_ready_cb(lv_event_t *event)
{
    (void)event;
    /* Warm the settings art during the still greeting, after GIF playback. */
    lv_timer_create(prewarm_settings_cb, UI_CORE_PREWARM_MS, NULL);
}

void ui_core_show_welcome(void)
{
    lv_obj_t *welcome = ui_screen_welcome_create();
    lv_obj_add_event_cb(welcome, welcome_ready_cb, LV_EVENT_READY, NULL);
    lv_screen_load(welcome);
}

void ui_core_show_eyes(void)
{
    load_screen(0);
}

void ui_core_notify_pickup(void)
{
    /* Screen off: motion must not trigger expressions or their sounds. */
    if (!s_standby && s_active_eyes_api && s_active_eyes_api->on_pickup) {
        s_active_eyes_api->on_pickup();
    }
}

void ui_core_notify_shake(float accel_x, float accel_y, bool active)
{
    /* Still forward the end of a shake so the eyes settle, but never start one in standby. */
    if (s_active_eyes_api && s_active_eyes_api->on_shake && (!s_standby || !active)) {
        s_active_eyes_api->on_shake(accel_x, accel_y, active);
    }
}

void ui_core_set_expression(ui_expression_t expression)
{
    if (s_active_eyes_api && s_active_eyes_api->set_expression) {
        s_active_eyes_api->set_expression((uint8_t)expression);
    }
}

static void update_leave_cb(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    s_updating = false;
    s_update_screen = NULL;
    load_screen(0);
}

static void update_show_async(void *arg)
{
    (void)arg;
    if (ui_panel_fade_active()) {
        lv_async_call(update_show_async, NULL);
        return;
    }
    /* The eyes and the settings shade draw straight to the panel; release them first. */
    if (s_active_eyes_api) {
        s_active_eyes_api->destroy();
        s_active_eyes_api = NULL;
    }
    if (s_settings_open) {
        ui_screen_settings_freeze();
        s_settings_open = false;
    }
    s_update_screen = ui_screen_update_create(s_update_version);
    lv_screen_load_anim(s_update_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    s_switching = false;
}

void ui_core_update_begin(const char *version)
{
    if (s_updating) {
        return;
    }
    s_updating = true;
    s_switching = true;
    snprintf(s_update_version, sizeof(s_update_version), "%s", version ? version : "");
    if (s_standby && s_settings_controls.wake) {
        s_settings_controls.wake();
    }
    lv_async_call(update_show_async, NULL);
}

void ui_core_update_progress(int percent)
{
    if (s_updating && s_update_screen) {
        ui_screen_update_set_progress(percent);
    }
}

void ui_core_update_fail(const char *reason)
{
    if (!s_updating) {
        return;
    }
    ui_screen_update_set_failed(reason);
    lv_timer_create(update_leave_cb, 4000, NULL);
}

void ui_core_update_done(void)
{
    if (s_updating) {
        ui_screen_update_set_done();
    }
}

void ui_core_set_standby(bool standby)
{
    s_standby = standby;
    /* Dark-screen touches must not adjust brightness or activate other controls. */
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_enable(indev, !standby);
            lv_indev_wait_release(indev);
        }
    }
    if (s_active_eyes_api && s_active_eyes_api->on_standby) {
        s_active_eyes_api->on_standby(standby);
    }
}
