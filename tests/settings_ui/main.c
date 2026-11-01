/* Host regression tests use real LVGL input, rotation, screens, controls and timers.
 * Only device services and widget rendering are substituted. */
#include "lvgl.h"
#include "ui_core.h"
#include "ui_screen_settings.h"
#include "ui_tools.h"
#include "eyes_renderer.h"
#include "net_wifi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

bool net_timezone_get(char *out, size_t size)
{
    snprintf(out, size, "Europe/Berlin");
    return true;
}

static uint8_t s_pixels[466 * 466 * 3];
static lv_indev_t *s_indev;
static lv_indev_data_t s_touch;
static lv_obj_t *s_eyes;
static bool s_panel_owned;
static int s_brightness = 100;
static int s_volume = 70;
static int s_brightness_commits;
static int s_volume_commits;
static bool s_dnd;
static bool s_sleeping;
static time_t s_now;

time_t time(time_t *out)
{
    if (out) {
        *out = s_now;
    }
    return s_now;
}

static net_wifi_status_t s_status = { .state = NET_WIFI_STARTING };
static int s_setup_requests;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(disp);
}

static void input_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    *data = s_touch;
}

static void advance(int ms)
{
    for (int i = 0; i < ms; i += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
}

static void touch(int x, int y, bool pressed)
{
    s_touch.point.x = 465 - y;
    s_touch.point.y = x;
    s_touch.state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    lv_indev_read(s_indev);
    lv_timer_pause(lv_indev_get_read_timer(s_indev));
    advance(20);
}

static void swipe(int x1, int y1, int x2, int y2)
{
    touch(x1, y1, true);
    for (int i = 1; i <= 8; i++) {
        touch(x1 + (x2 - x1) * i / 8, y1 + (y2 - y1) * i / 8, true);
    }
    touch(x2, y2, false);
    advance(600);
    lv_indev_read(s_indev);
    lv_timer_pause(lv_indev_get_read_timer(s_indev));
}

static void tap(int x, int y)
{
    touch(x, y, true);
    touch(x, y, false);
    advance(600);
    lv_indev_read(s_indev);
    lv_timer_pause(lv_indev_get_read_timer(s_indev));
}

static void eyes_create(lv_obj_t *scr)
{
    s_eyes = lv_obj_create(scr);
    lv_obj_set_size(s_eyes, 466, 466);
    lv_obj_clear_flag(s_eyes, LV_OBJ_FLAG_SCROLLABLE);
    /* Real renderer bypasses LVGL flushing. */
    s_panel_owned = true;
}

static void eyes_destroy(void)
{
    assert(s_panel_owned);
    s_panel_owned = false;
    lv_obj_delete(s_eyes);
    s_eyes = NULL;
}

static const eyes_renderer_api_t s_api = { .create = eyes_create, .destroy = eyes_destroy };
const eyes_renderer_api_t *eyes_renderer_get_default(void) { return &s_api; }

lv_obj_t *ui_screen_eyes_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    eyes_create(scr);
    return scr;
}

lv_obj_t *ui_screen_clock_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

const uint16_t *ui_screen_clock_panel_pixels(void) { return NULL; }
lv_obj_t *ui_screen_timer_create(void) { return lv_obj_create(NULL); }
const uint16_t *ui_screen_timer_panel_pixels(void) { return NULL; }
lv_obj_t *ui_screen_alarm_create(void) { return lv_obj_create(NULL); }
const uint16_t *ui_screen_alarm_panel_pixels(void) { return NULL; }
lv_obj_t *ui_screen_quote_create(void) { return lv_obj_create(NULL); }
const uint16_t *ui_screen_quote_panel_pixels(void) { return NULL; }
void net_remote_code(char out[7]) { memcpy(out, "123456", 7); }
void ui_tools_init(void (*show)(ui_tools_screen_t screen)) { (void)show; }
void ui_timer_set(int minutes, bool start) { (void)minutes; (void)start; }
void ui_alarm_set(int hour, int minute, bool enabled) { (void)hour; (void)minute; (void)enabled; }
lv_obj_t *ui_screen_welcome_create(void) { return lv_obj_create(NULL); }
void net_wifi_get_status(net_wifi_status_t *status) { *status = s_status; }
bool net_wifi_start_setup(void)
{
    s_setup_requests++;
    s_status.setup_active = true;
    s_status.state = NET_WIFI_SETUP;
    strcpy(s_status.setup_ssid, "Arixware-Setup-1234");
    strcpy(s_status.setup_password, "012345abcdef"); /* Synthetic fixture. gitleaks:allow */
    return true;
}
void audio_play_index(int index) { (void)index; }
static int brightness(void) { return s_brightness; }
static int volume(void) { return s_volume; }
static void set_brightness(int value, bool commit)
{
    assert(value >= 5 && value <= 100);
    s_brightness = value;
    s_brightness_commits += commit;
}
static void set_volume(int value, bool commit)
{
    assert(value >= 0 && value <= 100);
    s_volume = value;
    s_volume_commits += commit;
}

static bool get_dnd(void) { return s_dnd; }
static void set_dnd(bool enabled) { s_dnd = enabled; }
static void request_sleep(void)
{
    s_sleeping = true;
    ui_core_set_standby(true);
}

extern uint16_t g_panel_fb[466 * 466];
extern int g_panel_owners;
int ui_screen_settings_test_page(void);
void ui_screen_settings_test_info(char *out, size_t size);
int ui_screen_settings_test_wifi_stage(void);
bool ui_screen_settings_test_confirm_setup(void);
const char *ui_screen_settings_test_wifi_qr(void);

enum { PAGE_MAIN, PAGE_WIFI, PAGE_PORTAL, PAGE_ABOUT };
int ui_screen_settings_test_scroll(void);

static char s_info[512];
static const char *info(void)
{
    ui_screen_settings_test_info(s_info, sizeof(s_info));
    return s_info;
}

/* The panel must show exactly the shade's composed frame (the blits are byte-swapped). */
static void assert_panel_shows_settings(void)
{
    const uint16_t *frame = ui_screen_settings_panel_pixels();
    assert(frame);
    for (int i = 0; i < 466 * 466; i++) {
        int x = i % 466 - 233, y = i / 466 - 233;
        if (x * x + y * y < 230 * 230) {
            uint16_t v = frame[i];
            assert(g_panel_fb[i] == (uint16_t)((v >> 8) | (v << 8)));
        }
    }
}

static void assert_shade(void)
{
    assert(!s_panel_owned);
    assert(ui_screen_settings_panel_pixels());
    assert(g_panel_owners == 1); /* the shade owns the panel; no fade is still running */
    assert(ui_screen_settings_test_page() == PAGE_MAIN);
    assert_panel_shows_settings();
}

static void open_shade(void)
{
    swipe(233, 20, 233, 115);
    assert_shade();
}

static void write_preview(const char *path)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n466 466\n255\n");
    /* Upright RGB from the rotated, byte-swapped RGB565 panel. */
    for (int y = 0; y < 466; y++) {
        for (int x = 0; x < 466; x++) {
            uint16_t v = g_panel_fb[x * 466 + 465 - y];
            v = (uint16_t)((v >> 8) | (v << 8));
            fputc((v >> 8) & 0xF8, f);
            fputc((v >> 3) & 0xFC, f);
            fputc((v << 3) & 0xF8, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    lv_init();
    lv_display_t *disp = lv_display_create(466, 466);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(disp, s_pixels, NULL, sizeof(s_pixels), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_cb);
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, input_cb);
    lv_timer_pause(lv_indev_get_read_timer(s_indev));
    const ui_settings_controls_t controls = {
        .get_brightness = brightness,
        .get_volume = volume,
        .set_brightness = set_brightness,
        .set_volume = set_volume,
        .get_dnd = get_dnd,
        .set_dnd = set_dnd,
        .sleep = request_sleep,
    };
    ui_core_configure_settings(&controls);
    ui_core_show_welcome();
    advance(800); /* settings art is prewarmed while the splash holds still */
    ui_core_show_eyes();
    /* Do not refresh while the mock eyes own the display. Input still processes normally. */
    lv_timer_pause(lv_display_get_refr_timer(disp));
    advance(100);
    lv_obj_t *eyes_screen = lv_screen_active();
    swipe(233, 120, 233, 220);
    assert(lv_screen_active() == eyes_screen);
    open_shade();
    /* Back requires a rightward gesture that starts at the visual left edge. */
    swipe(233, 220, 350, 220);
    assert_shade();
    tap(96, 258);
    assert(ui_screen_settings_test_page() == PAGE_PORTAL);
    swipe(20, 233, 140, 233);
    assert_shade();
    tap(233, 364);
    assert(ui_screen_settings_test_page() == PAGE_ABOUT);
    swipe(20, 233, 140, 233);
    assert_shade();
    swipe(20, 233, 140, 233);
    assert(s_panel_owned && g_panel_owners == 0);
    open_shade();
    /* A first-boot wizard creates a small Back cache before any full-size Back.
     * Switching pages and reopening setup must resize those cached layers. */
    net_wifi_status_t initial_status = s_status;
    s_status.setup_active = true;
    strcpy(s_status.setup_ssid, "Arixware-Setup-1234");
    strcpy(s_status.setup_password, "012345abcdef"); /* Synthetic fixture. gitleaks:allow */
    tap(96, 152);
    assert(ui_screen_settings_test_wifi_stage() == 1);
    tap(233, 431);
    assert_shade();
    tap(96, 258);
    tap(233, 380);
    assert_shade();
    /* Reopen Wi-Fi repeatedly with a phone still joined to the hotspot. */
    s_status.setup_client_ready = true;
    s_status.state = NET_WIFI_CONNECTED;
    for (int reopen = 0; reopen < 10; reopen++) {
        tap(96, 152);
        assert(ui_screen_settings_test_wifi_stage() == 2);
        assert_panel_shows_settings();
        tap(233, 431);
        assert_shade();
    }
    s_status = initial_status;
    if (argc > 1) {
        write_preview(argv[1]);
    }
    for (int page = 2; argc > 2 && page < argc && page < 5; page++) {
        /* Extra paths: previews of the sub-pages, in button order. */
        static const int buttons[][2] = { { 96, 152 }, { 96, 258 }, { 233, 364 } };
        tap(buttons[page - 2][0], buttons[page - 2][1]);
        advance(400);
        write_preview(argv[page]);
        tap(page == 2 ? 148 : 233, page == 2 ? 354 : 372);
        advance(400);
    }

    /* Sliders drag relative to the touch, glide, clamp at their bounds and commit on release. */
    swipe(198, 180, 198, 330);
    assert(s_brightness == 5 && s_brightness_commits > 0);
    swipe(198, 270, 198, 118);
    assert(s_brightness == 100);
    swipe(268, 180, 268, 330);
    assert(s_volume == 0 && s_volume_commits > 0);
    swipe(268, 270, 268, 118);
    assert(s_volume == 100);
    advance(1500);
    assert_shade();

    tap(370, 152);
    assert(s_dnd && s_volume == 100);
    tap(370, 152);
    assert(!s_dnd && s_volume == 100);
    tap(370, 258);
    assert(s_sleeping);
    tap(96, 152);
    assert(ui_screen_settings_test_page() == PAGE_MAIN); /* Dark-screen touches do nothing. */
    ui_core_set_standby(false);
    s_sleeping = false;
    touch(96, 152, false);

    tap(96, 152);
    assert(ui_screen_settings_test_page() == PAGE_WIFI);
    assert(strstr(info(), "Status=Starting"));
    for (int state = NET_WIFI_SETUP; state <= NET_WIFI_DISCONNECTED; state++) {
        s_status.state = state;
        strcpy(s_status.ssid, "Test network");
        strcpy(s_status.ip, "192.168.4.1");
        advance(1100);
        assert(strstr(info(), "Network=Test network"));
        assert(strstr(info(), "IP address=192.168.4.1"));
    }
    assert_panel_shows_settings();
    tap(318, 362);
    assert(s_setup_requests == 1);
    advance(1100);
    assert(strstr(info(), "Scan to join setup Wi-Fi"));
    assert_panel_shows_settings();
    if (getenv("ARIXWARE_SETUP_PREVIEW")) write_preview(getenv("ARIXWARE_SETUP_PREVIEW"));
    assert(ui_screen_settings_test_wifi_stage() == 1);
    assert(!strncmp(ui_screen_settings_test_wifi_qr(), "WIFI:T:WPA;S:", 13));
    s_status.setup_client_ready = true;
    advance(1100);
    assert(ui_screen_settings_test_wifi_stage() == 2);
    assert(!strcmp(ui_screen_settings_test_wifi_qr(), "http://192.168.4.1/"));
    if (getenv("ARIXWARE_PORTAL_PREVIEW")) write_preview(getenv("ARIXWARE_PORTAL_PREVIEW"));
    /* A phone leaving returns the wizard to its join code. */
    s_status.setup_client_ready = false;
    advance(1100);
    assert(ui_screen_settings_test_wifi_stage() == 1);
    s_status.setup_client_ready = true;
    advance(1100);
    assert(ui_screen_settings_test_wifi_stage() == 2);
    s_status.state = NET_WIFI_CONNECTING;
    advance(1100);
    assert(ui_screen_settings_test_wifi_stage() == 3);
    size_t frame_size = 466 * 466 * sizeof(uint16_t);
    uint16_t *before = malloc(frame_size);
    assert(before);
    memcpy(before, ui_screen_settings_panel_pixels(), frame_size);
    advance(160);
    const uint16_t *after = ui_screen_settings_panel_pixels();
    int changed = 0;
    for (int y = 0; y < 466; y++) for (int x = 0; x < 466; x++) {
        if (before[y * 466 + x] != after[y * 466 + x]) {
            int dx = x - 233, dy = y - 233;
            assert(dx * dx + dy * dy > 215 * 215);
            changed++;
        }
    }
    assert(changed > 0);
    free(before);
    assert_panel_shows_settings();
    if (getenv("ARIXWARE_CONNECTING_PREVIEW")) write_preview(getenv("ARIXWARE_CONNECTING_PREVIEW"));
    /* A failed trial keeps the portal code available for retry. */
    s_status.state = NET_WIFI_SETUP;
    advance(1100);
    assert(ui_screen_settings_test_wifi_stage() == 2);
    s_status.state = NET_WIFI_CONNECTED;
    s_status.setup_complete = true;
    advance(1100);
    assert(ui_screen_settings_test_wifi_stage() == 4);
    assert(strstr(info(), "Connected!"));
    if (getenv("ARIXWARE_CONNECTED_PREVIEW")) write_preview(getenv("ARIXWARE_CONNECTED_PREVIEW"));
    s_status.setup_active = false;
    advance(1100);
    assert(strstr(info(), "Status=Connected"));
    int requests_before_confirm = s_setup_requests;
    tap(318, 362);
    assert(ui_screen_settings_test_confirm_setup());
    assert(s_setup_requests == requests_before_confirm);
    if (getenv("ARIXWARE_CONFIRM_PREVIEW")) write_preview(getenv("ARIXWARE_CONFIRM_PREVIEW"));
    tap(148, 344); /* No keeps the saved connection and hotspot state unchanged. */
    assert(!ui_screen_settings_test_confirm_setup());
    assert(!s_status.setup_active && s_setup_requests == requests_before_confirm);
    tap(318, 362);
    swipe(20, 233, 140, 233); /* Left-edge Back dismisses the prompt too. */
    assert(!ui_screen_settings_test_confirm_setup());
    tap(318, 362);
    tap(318, 344); /* Yes is the only action that requests setup again. */
    assert(s_setup_requests == requests_before_confirm + 1);
    advance(1100);
    s_status.state = NET_WIFI_CONNECTED;
    s_status.setup_complete = false;
    s_status.setup_client_ready = true;
    advance(1100);
    tap(233, 222); /* Tapping a displayed QR while connected has the same guard. */
    assert(ui_screen_settings_test_confirm_setup());
    tap(148, 344);
    assert(s_setup_requests == requests_before_confirm + 1);
    s_status.setup_active = false;
    advance(1100);
    if (getenv("ARIXWARE_WIFI_DETAILS_PREVIEW")) write_preview(getenv("ARIXWARE_WIFI_DETAILS_PREVIEW"));
    tap(148, 362);
    assert(ui_screen_settings_test_page() == PAGE_MAIN);

    tap(233, 364);
    assert(ui_screen_settings_test_page() == PAGE_ABOUT);
    assert(strstr(info(), "Time=Waiting for network"));
    s_now = 1793318400;
    s_status.time_synced = true;
    advance(1100);
    assert(strstr(info(), "2026"));
    /* Swiping up on a sub-page goes back instead of closing. */
    swipe(75, 275, 75, 135);
    assert_shade();

    net_wifi_status_t saved_status = s_status;
    s_status.state = NET_WIFI_DISCONNECTED;
    s_status.setup_active = false;
    s_status.ip[0] = 0;
    tap(96, 258);
    assert(ui_screen_settings_test_page() == PAGE_PORTAL && strstr(info(), "Status=Wi-Fi not connected"));
    assert(!ui_screen_settings_test_wifi_qr()[0]);
    tap(233, 380);
    assert(ui_screen_settings_test_page() == PAGE_MAIN);
    /* With Wi-Fi up, one QR code opens the phone app and carries the remote code. */
    s_status.state = NET_WIFI_CONNECTED;
    strcpy(s_status.ip, "192.168.1.20");
    tap(96, 258);
    assert(ui_screen_settings_test_page() == PAGE_PORTAL);
    assert(!strcmp(ui_screen_settings_test_wifi_qr(), "http://192.168.1.20/app?pair=123456"));
    assert_panel_shows_settings();
    tap(233, 431);
    assert(ui_screen_settings_test_page() == PAGE_MAIN);
    s_status = saved_status;
    advance(1100);
    tap(233, 364);
    assert(ui_screen_settings_test_page() == PAGE_ABOUT);
    const char *about = info();
    assert(strstr(about, "Brightness=100%"));
    assert(strstr(about, "Panel=1.75 in AMOLED"));
    assert(strstr(about, "Resolution=466 x 466"));
    assert(strstr(about, "Theme=Amber glow"));
    assert(strstr(about, "Device=Arixware buddy"));
    assert(strstr(about, "Firmware=host"));
    assert(strstr(about, "Timezone="));
    assert(strstr(about, "Wi-Fi=Connected"));
    assert(strstr(about, "IP address=192.168.4.1"));
    assert(strstr(about, "Phone app=http://192.168.4.1/app"));
    assert(strstr(about, "Remote code=123456"));
    assert(strstr(about, "Swipe down"));
    assert(ui_screen_settings_test_scroll() == 0);
    for (int k = 0; k < 5; k++) swipe(233, 280, 233, 130);
    assert(ui_screen_settings_test_page() == PAGE_ABOUT);
    int bottom = ui_screen_settings_test_scroll();
    assert(bottom == 13 * 34 + 232 - (306 - 118));
    swipe(233, 280, 233, 130);
    assert(ui_screen_settings_test_scroll() == bottom);
    advance(1100);
    assert(ui_screen_settings_test_scroll() == bottom);
    assert_panel_shows_settings();
    if (getenv("ARIXWARE_ABOUT_PREVIEW")) write_preview(getenv("ARIXWARE_ABOUT_PREVIEW"));
    for (int k = 0; k < 5; k++) swipe(233, 130, 233, 280);
    assert(ui_screen_settings_test_scroll() == 0);
    swipe(233, 280, 233, 130);
    assert(ui_screen_settings_test_scroll() > 0);
    tap(233, 380);
    tap(233, 364);
    assert(ui_screen_settings_test_scroll() == 0);
    tap(233, 380);
    assert_shade();

    /* A vertical slider drag must never close the shade. */
    swipe(198, 270, 198, 140);
    assert_shade();

    /* Close restores eyes and hands panel ownership back. */
    swipe(75, 275, 75, 135);
    assert(s_panel_owned && g_panel_owners == 0);
    /* Horizontal carousel remains available, then settings returns to the clock. */
    swipe(350, 230, 150, 230);
    assert(!s_panel_owned);
    open_shade();
    swipe(350, 280, 150, 280);
    assert(!s_sleeping);
    assert_shade();
    swipe(75, 275, 75, 135);
    assert(!s_panel_owned && g_panel_owners == 0);
    assert(lv_obj_get_child_count(lv_screen_active()) == 0);
    int baseline_timers = 0;
    for (lv_timer_t *timer = lv_timer_get_next(NULL); timer; timer = lv_timer_get_next(timer)) {
        baseline_timers++;
    }
    for (int i = 0; i < 30; i++) {
        open_shade();
        tap(96, 152);
        tap(148, 362);
        swipe(75, 275, 75, 135);
        assert(g_panel_owners == 0);
        assert(lv_obj_get_child_count(lv_screen_active()) == 0);
    }
    int remaining_timers = 0;
    for (lv_timer_t *timer = lv_timer_get_next(NULL); timer; timer = lv_timer_get_next(timer)) {
        remaining_timers++;
    }
    assert(remaining_timers == baseline_timers);
    puts("Settings UI: gestures, panel handoff, sliders, pages, navigation and lifecycle passed");
    void images_fixture_deinit(void);
    images_fixture_deinit();
    lv_deinit();
    return 0;
}
