#include "ui_screen_eyes.h"
#include "eyes_renderer.h"

#define TAP_SEQUENCE_TIMEOUT_MS 5000

static void eyes_pressed_cb(lv_event_t *e);
static bool touch_guard;
static bool returning_to_default;

lv_obj_t *ui_screen_eyes_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Built-in style for now; future eye packs register alternate renderers here. */
    eyes_renderer_get_default()->create(scr);

    lv_obj_add_event_cb(scr, eyes_pressed_cb, LV_EVENT_PRESSED, NULL);

    return scr;
}

static void reset_expression_cb(lv_timer_t *timer)
{
    if (returning_to_default) {
        touch_guard = false;
        returning_to_default = false;
        lv_timer_delete(timer);
        return;
    }
    const eyes_renderer_api_t *eyes = eyes_renderer_get_default();
    if (eyes->set_expression)
    {
        eyes->set_expression(EYES_EXPRESSION_DEFAULT);
    }
    /* Keep touches blocked while the eyes transition back to neutral. */
    returning_to_default = true;
    lv_timer_set_period(timer, 500);
}

static void eyes_pressed_cb(lv_event_t *e)
{
    (void)e;
    if (touch_guard) {
        return;
    }
    lv_timer_t *timer = lv_timer_create(reset_expression_cb, 1000, NULL);
    if (!timer) {
        return;
    }
    touch_guard = true;
    static int tap_count = 0;
    static uint32_t last_tap_at;
    /* Only accepted taps advance or extend the sequence. Tick subtraction handles wraparound. */
    if (lv_tick_elaps(last_tap_at) >= TAP_SEQUENCE_TIMEOUT_MS) {
        tap_count = 0;
    }
    last_tap_at = lv_tick_get();
    tap_count++;
    audio_play_index(MMAP_SOUNDS_TOUCH_WAV);

    const eyes_renderer_api_t *eyes = eyes_renderer_get_default();
    if (eyes->set_expression)
    {
        switch (tap_count)
        {
        case 1:
            eyes->set_expression(EYES_EXPRESSION_HAPPY);
            break;
        case 2:
            eyes->set_expression(EYES_EXPRESSION_SURPRISED);
            break;
        case 3:
            eyes->set_expression(EYES_EXPRESSION_NERVOUS);
            break;
        case 4:
            eyes->set_expression(EYES_EXPRESSION_SUSPICIOUS);
            break;
        case 5:
            eyes->set_expression(EYES_EXPRESSION_ANGRY);
            tap_count = 0;
            break;
        default:
            break;
        }
    }
}
