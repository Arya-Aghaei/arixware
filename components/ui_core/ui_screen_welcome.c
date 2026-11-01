#include "ui_screen_welcome.h"
#include "ui_core.h"
#include "ui_orient.h"
#include "ui_splash_asset.h"
#include "net_profile.h"
#include "fonts.h"

#define WELCOME_DWELL_MS 1800
#define WELCOME_INTRO_MS 400
#define SPLASH_WATCHDOG_MS 20000

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *gif;
    lv_obj_t *greeting;
    lv_timer_t *dwell;
    lv_timer_t *watchdog;
    bool finished;
} welcome_t;

static void set_label_opa(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void dwell_timeout_cb(lv_timer_t *timer)
{
    welcome_t *state = lv_timer_get_user_data(timer);
    state->dwell = NULL;
    lv_timer_delete(timer);
    /* The screen and state may be deleted synchronously by the transition. */
    ui_core_show_eyes();
}

static void finish_splash(welcome_t *state)
{
    if (state->finished) return;
    state->finished = true;
    if (state->watchdog) {
        lv_timer_delete(state->watchdog);
        state->watchdog = NULL;
    }
    lv_gif_pause(state->gif);
    /* Leave on the next timer tick, outside the GIF decoder's ready callback. */
    state->dwell = lv_timer_create(dwell_timeout_cb, 1, state);
    lv_obj_send_event(state->screen, LV_EVENT_READY, NULL);
}

static void splash_ready_cb(lv_event_t *event)
{
    finish_splash(lv_event_get_user_data(event));
}

static void watchdog_cb(lv_timer_t *timer)
{
    welcome_t *state = lv_timer_get_user_data(timer);
    state->watchdog = NULL;
    lv_timer_delete(timer);
    finish_splash(state);
}

static void delete_cb(lv_event_t *event)
{
    welcome_t *state = lv_event_get_user_data(event);
    if (state->dwell) lv_timer_delete(state->dwell);
    if (state->watchdog) lv_timer_delete(state->watchdog);
    lv_free(state);
}

lv_obj_t *ui_screen_welcome_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    welcome_t *state = lv_malloc_zeroed(sizeof(*state));
    LV_ASSERT_MALLOC(state);
    state->screen = scr;
    lv_obj_add_event_cb(scr, delete_cb, LV_EVENT_DELETE, state);

    state->gif = lv_gif_create(scr);
    lv_gif_set_color_format(state->gif, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(state->gif);

    char owner[NET_PROFILE_OWNER_MAX + 1];
    if (net_profile_get_owner(owner, sizeof(owner)) != ESP_OK || !owner[0])
        lv_strlcpy(owner, "User", sizeof(owner));
    lv_obj_t *mark = state->greeting = lv_label_create(scr);
    lv_label_set_text_fmt(mark, "Hello %s", owner);
    lv_obj_set_width(mark, 320);
    lv_label_set_long_mode(mark, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_height(mark, 96, 0);
    lv_obj_set_style_text_align(mark, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(mark, &fredoka_bold_40, 0);
    lv_obj_set_style_text_color(mark, lv_color_hex(0xFFE0AC), 0);
    lv_obj_set_style_opa(mark, LV_OPA_TRANSP, 0);
    /* Overlay the lower part of the splash, beneath its planet and brand mark. */
    lv_obj_align(mark, LV_ALIGN_CENTER, -122, 0);
    lv_obj_set_style_transform_pivot_x(mark, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(mark, lv_pct(50), 0);
    lv_obj_set_style_transform_rotation(mark, UI_ROTATION_DEG * 10, 0);

    lv_obj_add_event_cb(state->gif, splash_ready_cb, LV_EVENT_READY, state);
    state->watchdog = lv_timer_create(watchdog_cb, SPLASH_WATCHDOG_MS, state);
    const lv_image_dsc_t *asset = ui_splash_asset();
    if (asset) lv_gif_set_src(state->gif, asset);
    if (lv_gif_is_loaded(state->gif)) {
        lv_gif_set_loop_count(state->gif, 1);
        lv_obj_center(state->gif);
    } else {
        /* Without animation, keep the overlaid greeting readable before booting. */
        lv_timer_set_period(state->watchdog, WELCOME_DWELL_MS);
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, mark);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, WELCOME_INTRO_MS);
    lv_anim_set_exec_cb(&a, set_label_opa);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    return scr;
}
