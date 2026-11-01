#include "lvgl.h"
#include "ui_screen_welcome.h"
#include "net_profile.h"
#include "fonts.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *owner = "Arya";
static int eye_transitions;
int missing_asset;
static uint8_t pixels[466*466*3], framebuffer[466*466*3];
static int ready_events;

esp_err_t net_profile_get_owner(char *out, size_t size)
{
    out[0] = 0;
    if (!owner) return ESP_FAIL;
    snprintf(out, size, "%s", owner);
    return ESP_OK;
}
void ui_core_show_eyes(void) { eye_transitions++; }
static void ready_cb(lv_event_t *event) { (void)event; ready_events++; }
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    for (int y = area->y1; y <= area->y2; y++)
        memcpy(framebuffer+(y*466+area->x1)*3,
               data+(y-area->y1)*(area->x2-area->x1+1)*3,
               (area->x2-area->x1+1)*3);
    lv_display_flush_ready(display);
}
static void advance(int ms)
{
    for (int n = 0; n < ms; n += 5) {lv_tick_inc(5); lv_timer_handler();}
}
static lv_obj_t *start(void)
{
    lv_obj_t *screen = ui_screen_welcome_create();
    lv_obj_add_event_cb(screen, ready_cb, LV_EVENT_READY, NULL);
    lv_screen_load(screen);
    return screen;
}
static void dismiss(lv_obj_t *screen)
{
    lv_obj_t *blank = lv_obj_create(NULL);
    lv_screen_load(blank);
    lv_obj_delete(screen);
}
static void dump(void)
{
    FILE *out = fopen("/tmp/arixware-welcome.ppm", "wb");
    assert(out);
    fprintf(out, "P6\n466 466\n255\n");
    for (int i = 0; i < 466*466; i++) {
        fputc(framebuffer[i*3+2], out); fputc(framebuffer[i*3+1], out); fputc(framebuffer[i*3], out);
    }
    fclose(out);
}
int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(466,466);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    lv_obj_t *screen = start();
    lv_obj_t *gif = lv_obj_get_child(screen, 0), *label = lv_obj_get_child(screen, 1);
    assert(lv_gif_is_loaded(gif));
    assert(strcmp(lv_label_get_text(label), "Hello Arya") == 0);
    advance(3000);
    assert(eye_transitions == 0 && ready_events == 0);
    assert(lv_gif_get_current_frame_index(gif) > 20);
    assert(lv_obj_get_style_opa(label, 0) == LV_OPA_COVER);
    assert(lv_obj_get_style_text_font(label, 0) == &fredoka_bold_40);
    dump(); /* Greeting is visible while the actual splash is still playing. */
    advance(2500);
    assert(eye_transitions == 1 && ready_events == 1);
    dismiss(screen);
    advance(22000);
    assert(eye_transitions == 1); /* No watchdog or duplicate transitions after completion. */

    owner = NULL;
    screen = start();
    assert(strcmp(lv_label_get_text(lv_obj_get_child(screen, 1)), "Hello User") == 0);
    advance(500);
    dismiss(screen);
    advance(22000);
    assert(eye_transitions == 1); /* Deleting mid-playback cancels its timers. */

    owner = "";
    missing_asset = 1;
    screen = start();
    assert(strcmp(lv_label_get_text(lv_obj_get_child(screen, 1)), "Hello User") == 0);
    advance(2400);
    assert(eye_transitions == 2); /* Missing asset still greets and boots. */
    dismiss(screen);
    puts("PASS: real splash playback with clock-font greeting overlay, fallback, dwell and timer cleanup");
    return 0;
}
