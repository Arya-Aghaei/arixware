#include "ui_screen_settings.h"
#include "ui_panel.h"
#include "eyes_panel.h"
#include "ui_orient.h"
#include "fonts.h"
#include "net_wifi.h"
#include "net_remote.h"
#include "net_ota.h"
#include "ui_draw.h"
#include "eyes_theme.h"
#include "net_timezone.h"
#include "src/libs/qrcode/qrcodegen.h"
#include "images.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#endif

/* The settings shade is drawn like the eyes: an upright frame is composed in PSRAM, only the
 * changed rectangles are rotated into a panel-ordered copy and sent straight to the panel. LVGL
 * only delivers touch input; per-object LVGL rotation and full-screen LVGL fades were far too
 * slow on this panel and made opening flicker and the sliders lag. */

#define FACE_SIZE UI_PANEL_SIZE
#define FRAME_MS 16
#define INFO_MS 1000
#define PAGE_FADE_MS 240
#define STRIP_PIXELS 4096

#define COLOR_TEXT 0xFFF4DF
#define COLOR_SECONDARY 0xFFD49B
#define COLOR_PRIMARY 0xFF9347
#define COLOR_GLOW 0xFF8B24
#define COLOR_ON_CREAM 0x78421E

#define SLIDER_W 62
#define SLIDER_TOP 100
#define SLIDER_BOTTOM 292
#define SLIDER_INSET 4
#define KNOB_R 24
#define FILL_MIN (2 * KNOB_R + 6)
#define FILL_MAX (SLIDER_BOTTOM - SLIDER_TOP - 2 * SLIDER_INSET)
#define VALUE_SHOW_MS 900

#define BODY_X0 70
#define BODY_X1 396
#define BODY_Y0 118
#define BODY_Y1 306
#define ROW_LEFT 76
#define ROW_RIGHT 390
#define ROW_HEIGHT 34
#define MAX_ROWS 14
#define ABOUT_FOOTER_H 232 /* Help note, spacing, and the maker signature. */

typedef struct
{
    int x0, y0, x1, y1; /* upright pixels, end-exclusive */
} rect_t;

typedef enum
{
    ICON_WIFI,
    ICON_PORTAL,
    ICON_MOON,
    ICON_SLEEP,
    ICON_PALETTE,
    ICON_GEAR,
    ICON_MORE,
    ICON_BACK,
    ICON_SUN,
    ICON_VOLUME,
    ICON_UPDATE,
    ICON_AUTO,
    ICON_EYES,
    ICON_NEXT,
    ICON_SQUARE,
} settings_icon_t;

typedef enum
{
    PAGE_MAIN,
    PAGE_WIFI,
    PAGE_PORTAL,
    PAGE_ABOUT,
    PAGE_UPDATE,
    PAGE_EYES,
} page_t;

typedef enum
{
    BTN_WIFI,
    BTN_PORTAL,
    BTN_DND,
    BTN_SLEEP,
    BTN_ABOUT,
    BTN_UPDATE,
    BTN_EYES,
    BTN_EYES_PREV,
    BTN_EYES_NEXT,
    BTN_EYES_STYLE,
    BTN_UPDATE_CHECK,
    BTN_UPDATE_AUTO,
    BTN_BACK,
    BTN_WIFI_SETUP,
    BTN_SETUP_NO,
    BTN_SETUP_YES,
    BTN_COUNT,
} button_id_t;

typedef struct
{
    const char *caption;
    int16_t cx, cy, size;
    settings_icon_t icon;
} button_def_t;

static const button_def_t BUTTONS[BTN_COUNT] = {
    [BTN_WIFI] = {"Wi-Fi", 102, 138, 74, ICON_WIFI},
    [BTN_PORTAL] = {"Portal", 102, 246, 74, ICON_PORTAL},
    [BTN_DND] = {"DND", 364, 138, 74, ICON_MOON},
    [BTN_SLEEP] = {"Sleep", 364, 246, 74, ICON_SLEEP},
    [BTN_ABOUT] = {"About", 144, 352, 60, ICON_GEAR},
    [BTN_EYES] = {"Eyes", 233, 372, 60, ICON_EYES},
    [BTN_UPDATE] = {"Update", 322, 352, 60, ICON_UPDATE},
    [BTN_EYES_PREV] = {"", 72, 236, 56, ICON_BACK},
    [BTN_EYES_NEXT] = {"", 394, 236, 56, ICON_NEXT},
    [BTN_EYES_STYLE] = {"Style", 318, 354, 60, ICON_SQUARE},
    [BTN_UPDATE_CHECK] = {"Check", 233, 354, 66, ICON_UPDATE},
    [BTN_UPDATE_AUTO] = {"Auto", 348, 354, 60, ICON_AUTO},
    [BTN_BACK] = {"Back", 233, 372, 68, ICON_BACK},
    [BTN_WIFI_SETUP] = {"Set up", 233, 284, 60, ICON_WIFI},
    [BTN_SETUP_NO] = {"No", 148, 344, 68, ICON_BACK},
    [BTN_SETUP_YES] = {"Yes", 318, 344, 68, ICON_WIFI},
};

static const int SLIDER_CX[2] = {198, 268};

typedef struct
{
    char label[20];
    char value[64]; /* Includes full IANA timezone names. */
} info_row_t;

typedef struct
{
    int count;
    info_row_t rows[MAX_ROWS];
    char note[96];
    char wifi_qr[128];
    int wifi_stage; /* 1: join hotspot, 2: setup page, 3: connecting, 4: saved, 5: phone portal pairing */
    char setup_ssid[33];
    char setup_password[13];
} info_t;

static struct
{
    ui_settings_controls_t controls;
    lv_obj_t *scr;
    lv_obj_t *canvas;      /* shows `panel` whenever LVGL owns the display */
    lv_obj_t *draw_canvas; /* hidden; LVGL draws text, icons and skins into `work` */
    uint16_t *work;        /* upright frame */
    uint16_t *panel;       /* the same frame in panel orientation */
    uint16_t *prev;        /* previous page while a page cross-fade runs */
    ui_panel_strips_t strips;
    bool direct;
    bool frozen;
    bool page_fading;
    lv_timer_t *frame_timer;
    lv_timer_t *info_timer;
    uint32_t last_tick;
    page_t page;
    info_t info;
    int info_scroll;
    int info_touch_y;
    bool info_drag;
    bool confirm_setup;
    bool qr_pressed;
    int spinner_angle;
    uint32_t spinner_tick;

    int value[2];
    int applied[2];
    float level[2];
    float label_alpha[2];
    uint32_t released_at[2];
    int drag;
    int drag_start_y;
    int drag_start_value;

    int pressed;
    bool press_cancelled;
    lv_point_t press_start;
    uint32_t tap_until[BTN_COUNT];
    float press[BTN_COUNT];
    float active[BTN_COUNT];

    rect_t dirty[8];
    int dirty_count;
} s;

static inline float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool rect_intersect(rect_t *out, rect_t a, rect_t b)
{
    out->x0 = LV_MAX(a.x0, b.x0);
    out->y0 = LV_MAX(a.y0, b.y0);
    out->x1 = LV_MIN(a.x1, b.x1);
    out->y1 = LV_MIN(a.y1, b.y1);
    return out->x0 < out->x1 && out->y0 < out->y1;
}

static bool rect_overlaps(rect_t a, rect_t b)
{
    rect_t unused;
    return rect_intersect(&unused, a, b);
}

static inline void to_panel(int lx, int ly, int *px, int *py)
{
    switch (UI_ROTATION_DEG)
    {
    case 90:
        *px = FACE_SIZE - 1 - ly;
        *py = lx;
        break;
    case 180:
        *px = FACE_SIZE - 1 - lx;
        *py = FACE_SIZE - 1 - ly;
        break;
    case 270:
        *px = ly;
        *py = FACE_SIZE - 1 - lx;
        break;
    default:
        *px = lx;
        *py = ly;
        break;
    }
}

static inline void to_upright(int px, int py, int *lx, int *ly)
{
    switch (UI_ROTATION_DEG)
    {
    case 90:
        *lx = py;
        *ly = FACE_SIZE - 1 - px;
        break;
    case 180:
        *lx = FACE_SIZE - 1 - px;
        *ly = FACE_SIZE - 1 - py;
        break;
    case 270:
        *lx = FACE_SIZE - 1 - py;
        *ly = px;
        break;
    default:
        *lx = px;
        *ly = py;
        break;
    }
}

static inline uint16_t pack565(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* Alpha-blend a 0xRRGGBB colour onto an RGB565 pixel; alpha 0..1. */
static inline void blend_px(uint16_t *dst, uint32_t rgb, float alpha)
{
    int a = (int)(alpha * 256.0f + 0.5f);
    if (a <= 0)
    {
        return;
    }
    if (a > 256)
    {
        a = 256;
    }
    uint16_t d = *dst;
    int dr = (d >> 8) & 0xF8, dg = (d >> 3) & 0xFC, db = (d << 3) & 0xF8;
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    *dst = pack565((uint32_t)(dr + (((r - dr) * a) >> 8)), (uint32_t)(dg + (((g - dg) * a) >> 8)),
                   (uint32_t)(db + (((b - db) * a) >> 8)));
}

static inline uint32_t mix_rgb(uint32_t a, uint32_t b, float t)
{
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int k = (int)(clamp01(t) * 256.0f);
    return (uint32_t)(((ar + (((br - ar) * k) >> 8)) << 16) | ((ag + (((bg - ag) * k) >> 8)) << 8) |
                      (ab + (((bb - ab) * k) >> 8)));
}

/* Signed distance to a rounded rectangle (negative inside). */
static inline float sd_round_rect(float px, float py, float x0, float y0, float x1, float y1, float r)
{
    float hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f;
    r = LV_MIN(r, LV_MIN(hw, hh));
    float qx = fabsf(px - (x0 + hw)) - (hw - r);
    float qy = fabsf(py - (y0 + hh)) - (hh - r);
    float ox = LV_MAX(qx, 0.0f), oy = LV_MAX(qy, 0.0f);
    float outside = (ox > 0.0f || oy > 0.0f) ? sqrtf(ox * ox + oy * oy) : 0.0f;
    return outside + LV_MIN(LV_MAX(qx, qy), 0.0f) - r;
}

static void *alloc_frame(size_t size)
{
#ifdef ESP_PLATFORM
    /* PSRAM: internal RAM is a scarce pool shared with the SPI driver's DMA descriptors. */
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

static void free_frame(void *p)
{
#ifdef ESP_PLATFORM
    heap_caps_free(p);
#else
    free(p);
#endif
}

/* ---------------------------------------------------------------- geometry */

static const button_def_t *button_def(int id)
{
    static const button_def_t wifi_back = {"Back", 148, 354, 68, ICON_BACK};
    static const button_def_t wifi_setup = {"Set up", 318, 354, 68, ICON_WIFI};
    static const button_def_t wizard_back = {"Back", 233, 431, 30, ICON_BACK};
    static const button_def_t update_back = {"Back", 118, 356, 60, ICON_BACK};
    static const button_def_t eyes_back = {"Back", 148, 354, 60, ICON_BACK};
    if (s.page == PAGE_PORTAL && id == BTN_BACK && s.info.wifi_qr[0]) return &wizard_back;
    if (s.page == PAGE_UPDATE && id == BTN_BACK) return &update_back;
    if (s.page == PAGE_EYES && id == BTN_BACK) return &eyes_back;
    if (s.page == PAGE_WIFI) {
        if (id == BTN_BACK) return s.info.wifi_qr[0] ? &wizard_back : &wifi_back;
        if (id == BTN_WIFI_SETUP) return &wifi_setup;
    }
    return &BUTTONS[id];
}


static bool button_on_page(int id)
{
    if (s.confirm_setup) return id == BTN_SETUP_NO || id == BTN_SETUP_YES;
    if (id == BTN_SETUP_NO || id == BTN_SETUP_YES) return false;
    if (id == BTN_WIFI_SETUP) return s.page == PAGE_WIFI && !s.info.wifi_qr[0];
    if (id == BTN_UPDATE_CHECK || id == BTN_UPDATE_AUTO) return s.page == PAGE_UPDATE;
    if (id == BTN_EYES_PREV || id == BTN_EYES_NEXT || id == BTN_EYES_STYLE) return s.page == PAGE_EYES;
    return s.page == PAGE_MAIN ? id != BTN_BACK : id == BTN_BACK;
}

static rect_t button_rect(int id)
{
    const button_def_t *b = button_def(id);
    int half = b->size * 43 / 66 + 3; /* the 86 px skin, glow included, scaled to the button */
    return (rect_t){b->cx - half, b->cy - half, b->cx + half + 1, b->cy + half + 1};
}

static rect_t slider_rect(int i)
{
    return (rect_t){SLIDER_CX[i] - SLIDER_W / 2 - 8, SLIDER_TOP - 8, SLIDER_CX[i] + SLIDER_W / 2 + 8,
                    SLIDER_BOTTOM + 8};
}

/* What a level change can touch: the glow above the track and the title never change. */
static rect_t slider_dirty_rect(int i)
{
    rect_t r = slider_rect(i);
    r.y0 = SLIDER_TOP;
    return r;
}

static rect_t body_rect(void)
{
    return (rect_t){BODY_X0, BODY_Y0, BODY_X1, BODY_Y1};
}

static int slider_min(int i)
{
    return i == 0 ? 5 : 0;
}

static float level_of(int i, int value)
{
    return (float)(value - slider_min(i)) / (float)(100 - slider_min(i));
}

static float fill_top(int i)
{
    float height = FILL_MIN + s.level[i] * (FILL_MAX - FILL_MIN);
    return (float)(SLIDER_BOTTOM - SLIDER_INSET) - height;
}

/* ---------------------------------------------------------------- info pages */

static const char *page_title(page_t page)
{
    switch (page)
    {
    case PAGE_WIFI:
        if (s.confirm_setup) return "Wi-Fi setup";
        switch (s.info.wifi_stage) {
        case 1: return "1. Join Wi-Fi";
        case 2: return "2. Open setup";
        case 3: return "Connecting";
        case 4: return "Connected";
        default: return "Wi-Fi";
        }
    case PAGE_PORTAL:
        return "Portal";
    case PAGE_ABOUT:
        return "About";
    case PAGE_UPDATE:
        return "Update";
    case PAGE_EYES:
        return "Eyes";
    default:
        return "Settings";
    }
}

static void add_row(info_t *info, const char *label, const char *value)
{
    if (info->count < MAX_ROWS)
    {
        info_row_t *row = &info->rows[info->count++];
        snprintf(row->label, sizeof(row->label), "%s", label);
        snprintf(row->value, sizeof(row->value), "%s", value);
    }
}

static const char *wifi_state_text(net_wifi_state_t state)
{
    switch (state)
    {
    case NET_WIFI_SETUP:
        return "Setup network";
    case NET_WIFI_CONNECTING:
        return "Connecting";
    case NET_WIFI_CONNECTED:
        return "Connected";
    case NET_WIFI_DISCONNECTED:
        return "Retrying";
    default:
        return "Starting";
    }
}

static const char *firmware_version(void)
{
#ifdef ESP_PLATFORM
    return esp_app_get_description()->version;
#else
    return "host";
#endif
}

static void build_info(page_t page, info_t *info)
{
    memset(info, 0, sizeof(*info));
    net_wifi_status_t wifi;
    net_wifi_get_status(&wifi);
    char text[48];
    switch (page)
    {
    case PAGE_WIFI:
        if (wifi.setup_active) {
            info->wifi_stage = wifi.setup_complete ? 4 : wifi.state == NET_WIFI_CONNECTING ? 3 :
                               wifi.setup_client_ready ? 2 : 1;
            if (info->wifi_stage == 1) {
                snprintf(info->wifi_qr, sizeof(info->wifi_qr), "WIFI:T:WPA;S:%s;P:%s;;", wifi.setup_ssid, wifi.setup_password);
            } else {
                snprintf(info->wifi_qr, sizeof(info->wifi_qr), "http://192.168.4.1/");
            }
            snprintf(info->setup_ssid, sizeof(info->setup_ssid), "%s", wifi.setup_ssid);
            snprintf(info->setup_password, sizeof(info->setup_password), "%s", wifi.setup_password);
            const char *step = info->wifi_stage == 1 ? "Scan to join setup Wi-Fi" :
                               info->wifi_stage == 2 ? "Scan to open the setup page" :
                               info->wifi_stage == 3 ? "Connecting to your network" : "Connected!";
            snprintf(info->note, sizeof(info->note), "%s", step);
            break;
        }
        add_row(info, "Status", wifi_state_text(wifi.state));
        add_row(info, "Network", wifi.ssid[0] ? wifi.ssid : "Unavailable");
        add_row(info, "IP address", wifi.ip[0] ? wifi.ip : "Unavailable");
        add_row(info, "Time sync", wifi.time_synced ? "Synchronized" : "Waiting");
        break;
    case PAGE_PORTAL:
        if (wifi.state == NET_WIFI_CONNECTED && wifi.ip[0] && !wifi.setup_active) {
            /* One scan opens the phone app and hands it the remote code. */
            char code[7];
            net_remote_code(code);
            info->wifi_stage = 5;
            snprintf(info->wifi_qr, sizeof(info->wifi_qr), "http://%s/app?pair=%s", wifi.ip, code);
            snprintf(info->setup_ssid, sizeof(info->setup_ssid), "http://%s/app", wifi.ip);
        } else {
            add_row(info, "Status", wifi.state == NET_WIFI_CONNECTING ? "Connecting" : "Wi-Fi not connected");
            snprintf(info->note, sizeof(info->note), "Join Wi-Fi first (Settings > Wi-Fi). Then a QR code here opens the phone remote.");
        }
        break;
    case PAGE_ABOUT:
    {
        add_row(info, "Device", "Arixware buddy");
        snprintf(text, sizeof(text), "%d%%", s.value[0]);
        add_row(info, "Brightness", text);
        add_row(info, "Panel", "1.75 in AMOLED");
        add_row(info, "Resolution", "466 x 466");
        add_row(info, "Theme", "Amber glow");
        time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        if (local.tm_year + 1900 < 2020)
        {
            add_row(info, "Time", "Waiting for network");
        }
        else
        {
            strftime(text, sizeof(text), "%H:%M:%S", &local);
            add_row(info, "Time", text);
            strftime(text, sizeof(text), "%a, %d %b %Y", &local);
            add_row(info, "Date", text);
        }
        char timezone[NET_TIMEZONE_NAME_MAX+1];
        add_row(info, "Timezone", net_timezone_get(timezone, sizeof(timezone)) ? timezone : "Detecting...");
        add_row(info, "Wi-Fi", wifi_state_text(wifi.state));
        add_row(info, "IP address", wifi.ip[0] ? wifi.ip : "Unavailable");
        add_row(info, "Firmware", firmware_version());
        if (wifi.ip[0]) {
            snprintf(text, sizeof(text), "http://%s/app", wifi.ip);
            add_row(info, "Phone app", text);
        }
        char remote_code[7];
        net_remote_code(remote_code);
        add_row(info, "Remote code", remote_code);
        snprintf(info->note, sizeof(info->note),
                 "Swipe down from the top edge to open settings. Scroll for details; tap Back to return.");
        break;
    }
    case PAGE_UPDATE:
    {
        net_ota_status_t ota;
        net_ota_get_status(&ota);
        add_row(info, "Version", firmware_version());
        add_row(info, "Latest", ota.latest[0] ? ota.latest : "Not checked");
        switch (ota.state)
        {
        case NET_OTA_CHECKING: snprintf(text, sizeof(text), "Checking..."); break;
        case NET_OTA_CURRENT: snprintf(text, sizeof(text), "Up to date"); break;
        case NET_OTA_INSTALLING: snprintf(text, sizeof(text), "Installing %d%%", ota.progress); break;
        case NET_OTA_DONE: snprintf(text, sizeof(text), "Restarting"); break;
        case NET_OTA_FAILED: snprintf(text, sizeof(text), "%.40s", ota.error[0] ? ota.error : "Failed"); break;
        default: snprintf(text, sizeof(text), wifi.state == NET_WIFI_CONNECTED ? "Ready" : "No Wi-Fi"); break;
        }
        add_row(info, "Status", text);
        add_row(info, "Auto update", net_ota_get_auto() ? "On" : "Off");
        break;
    }
    default:
        break;
    }
}

/* ---------------------------------------------------------------- drawing */

#define SLIDER_RECT_W (SLIDER_W + 16)
#define SLIDER_RECT_H (SLIDER_BOTTOM - SLIDER_TOP + 16)

/* Static art kept across openings (PSRAM): the upright background, each slider shaded empty and
 * completely full over it, and each button area resting and lit. A drag frame only picks rows
 * from the two slider layers and shades the few rows around the fill's rounded top; a button
 * animation only mixes its two layers. */
static struct
{
    uint16_t *bg;
    uint16_t *slider[2][2];         /* [slider][0 = empty, 1 = full] */
    uint16_t *button[BTN_COUNT][2]; /* [button][0 = resting, 1 = lit], built on first use */
    bool button_valid[BTN_COUNT];
    size_t button_pixels[BTN_COUNT][2]; /* allocated capacity, independent of current geometry */
    uint16_t *main; /* the composed main page; sliders and DND are redrawn over it on use */
} s_cache;

static void blend565_px(uint16_t *dst, uint16_t src, int alpha256)
{
    uint16_t d = *dst;
    int dr = (d >> 11) & 0x1F, dg = (d >> 5) & 0x3F, db = d & 0x1F;
    int sr = (src >> 11) & 0x1F, sg = (src >> 5) & 0x3F, sb = src & 0x1F;
    *dst = (uint16_t)(((dr + (((sr - dr) * alpha256) >> 8)) << 11) | ((dg + (((sg - dg) * alpha256) >> 8)) << 5) |
                      (db + (((sb - db) * alpha256) >> 8)));
}

/* The background art is upright; panel rotation happens when presenting the composed frame. */
static bool build_background(void)
{
    if (s_cache.bg)
    {
        return true;
    }
    s_cache.bg = alloc_frame((size_t)FACE_SIZE * FACE_SIZE * sizeof(uint16_t));
    if (!s_cache.bg)
    {
        return false;
    }
    const lv_image_dsc_t *bg = images_get(IMAGE_SETTINGS_BACKGROUND);
    if (!bg)
    {
        memset(s_cache.bg, 0, (size_t)FACE_SIZE * FACE_SIZE * sizeof(uint16_t));
        return true;
    }
    for (int ty = 0; ty < FACE_SIZE; ty += 16)
    {
        for (int tx = 0; tx < FACE_SIZE; tx += 16)
        {
            for (int y = ty; y < LV_MIN(ty + 16, FACE_SIZE); y++)
            {
                for (int x = tx; x < LV_MIN(tx + 16, FACE_SIZE); x++)
                {
                    const uint8_t *p = bg->data + (size_t)y * bg->header.stride + x * 3; /* B, G, R */
                    s_cache.bg[(size_t)y * FACE_SIZE + x] = pack565(p[2], p[1], p[0]);
                }
            }
        }
    }
    return true;
}

static void draw_background(rect_t r)
{
    for (int y = r.y0; y < r.y1; y++)
    {
        memcpy(s.work + (size_t)y * FACE_SIZE + r.x0, s_cache.bg + (size_t)y * FACE_SIZE + r.x0,
               (size_t)(r.x1 - r.x0) * sizeof(uint16_t));
    }
}

/* Glassy track, glossy halftone fill (when `full`) and cream knob, shaded per pixel over the
 * background into a slider-sized layer. The fill colours depend only on the absolute height, so
 * any level is a mix of the empty and full layers. */
static void shade_slider(uint16_t *layer, int i, bool full)
{
    const rect_t box = slider_rect(i);
    const float cx = (float)SLIDER_CX[i];
    const float x0 = cx - SLIDER_W / 2.0f, x1 = cx + SLIDER_W / 2.0f;
    const float y0 = SLIDER_TOP, y1 = SLIDER_BOTTOM, radius = SLIDER_W / 2.0f;
    const float ix0 = x0 + SLIDER_INSET, ix1 = x1 - SLIDER_INSET, iy1 = y1 - SLIDER_INSET;
    const float top = iy1 - FILL_MAX;
    const float kx = cx, ky = iy1 - 2.0f - KNOB_R;
    const float gloss_x = ix0 + 9.0f;
    const int dot_x0 = (int)ix0 + 3, dot_y0 = (int)iy1 - 3;

    for (int py = box.y0; py < box.y1; py++)
    {
        const float fy = py + 0.5f;
        uint16_t *row = layer + (size_t)(py - box.y0) * SLIDER_RECT_W;
        memcpy(row, s_cache.bg + (size_t)py * FACE_SIZE + box.x0, SLIDER_RECT_W * sizeof(uint16_t));
        const float ty = clamp01((fy - y0) / (y1 - y0));
        const uint32_t body = mix_rgb(0x2E180B, 0x140A04, ty);
        const uint32_t border = mix_rgb(0xD0732E, 0x8A4016, ty);
        const float ft = clamp01((fy - top) / FILL_MAX);
        const uint32_t fill = mix_rgb(0xFFE6B4, 0xFF9038, ft);
        const float dot_r = 0.25f + 1.55f * ft;
        const float gloss_v = clamp01((fy - top - 4.0f) / 10.0f) * clamp01((iy1 - 6.0f - fy) / 10.0f);
        int dy = ((dot_y0 - py) % 6 + 6) % 6;
        dy = dy > 3 ? 6 - dy : dy;
        for (int px = box.x0; px < box.x1; px++)
        {
            const float fx = px + 0.5f;
            uint16_t *d = &row[px - box.x0];
            float dt = sd_round_rect(fx, fy, x0, y0, x1, y1, radius);
            if (dt > 0.0f)
            {
                if (dt < 8.0f)
                {
                    float g = 1.0f - dt / 8.0f;
                    blend_px(d, COLOR_GLOW, 0.22f * g * g);
                }
                if (dt >= 0.5f)
                {
                    continue;
                }
            }
            float cov = clamp01(0.5f - dt);
            blend_px(d, body, 0.92f * cov);
            blend_px(d, border, clamp01(1.0f - fabsf(dt + 1.2f) / 1.2f) * cov);

            if (full)
            {
                float fcov = clamp01(0.5f - sd_round_rect(fx, fy, ix0, top, ix1, iy1, radius - SLIDER_INSET));
                if (fcov > 0.0f)
                {
                    uint32_t c = fill;
                    int dx = ((px - dot_x0) % 6 + 6) % 6;
                    dx = dx > 3 ? 6 - dx : dx;
                    float d2 = (float)(dx * dx + dy * dy);
                    if (d2 < (dot_r + 1.0f) * (dot_r + 1.0f))
                    {
                        c = mix_rgb(c, 0xFFF6E0, 0.5f * clamp01(dot_r + 0.5f - sqrtf(d2)));
                    }
                    float gl = (fx - gloss_x) / 3.5f;
                    gl = clamp01(1.0f - gl * gl);
                    c = mix_rgb(c, 0xFFFBEE, 0.42f * gl * gl * gloss_v);
                    c = mix_rgb(c, 0xC4561B, 0.22f * clamp01((fx - (cx + 6.0f)) / 15.0f));
                    blend_px(d, c, fcov);
                }
            }

            float kdx = fx - kx, kdy = fy - ky;
            if (fabsf(kdx) > KNOB_R + 6.0f || kdy < -KNOB_R - 6.0f || kdy > KNOB_R + 8.0f)
            {
                continue;
            }
            float kd = sqrtf(kdx * kdx + kdy * kdy);
            float shadow = sqrtf(kdx * kdx + (kdy - 2.5f) * (kdy - 2.5f)) - KNOB_R;
            if (shadow < 5.0f)
            {
                float sg = clamp01(1.0f - shadow / 5.0f);
                blend_px(d, 0x7A3410, 0.4f * sg * sg);
            }
            float kcov = clamp01(KNOB_R + 0.5f - kd);
            if (kcov > 0.0f)
            {
                float hx = fx - (kx - 6.0f), hy = fy - (ky - 7.0f);
                float u = clamp01(sqrtf(hx * hx + hy * hy) / (KNOB_R * 1.6f));
                uint32_t c = mix_rgb(0xFFFAEC, 0xFFCF8A, u);
                c = mix_rgb(c, 0xFFFFFF, 0.45f * clamp01(1.0f - fabsf(kd - (KNOB_R - 1.2f))));
                blend_px(d, c, kcov);
            }
        }
    }
}

static bool build_caches(void)
{
    if (!build_background())
    {
        return false;
    }
    for (int i = 0; i < 2; i++)
    {
        for (int full = 0; full < 2; full++)
        {
            if (!s_cache.slider[i][full])
            {
                s_cache.slider[i][full] = alloc_frame(SLIDER_RECT_W * SLIDER_RECT_H * sizeof(uint16_t));
                if (!s_cache.slider[i][full])
                {
                    return false;
                }
                shade_slider(s_cache.slider[i][full], i, full);
            }
        }
    }
    return true;
}

/* Rows below the fill's rounded top come from the full layer, rows above from the empty one;
 * only the cap rows are mixed per pixel, plus a bright meniscus along the edge. */
static void draw_slider_body(int i, rect_t clip)
{
    rect_t box = slider_rect(i);
    rect_t r;
    if (!rect_intersect(&r, box, clip))
    {
        return;
    }
    const float cx = (float)SLIDER_CX[i];
    const float ix0 = cx - SLIDER_W / 2.0f + SLIDER_INSET, ix1 = cx + SLIDER_W / 2.0f - SLIDER_INSET;
    const float iy1 = SLIDER_BOTTOM - SLIDER_INSET;
    const float in_r = SLIDER_W / 2.0f - SLIDER_INSET;
    const float top = fill_top(i);
    const int cap_y0 = (int)floorf(top) - 1, cap_y1 = (int)ceilf(top + in_r) + 1;
    const uint16_t *empty = s_cache.slider[i][0], *full = s_cache.slider[i][1];
    const uint16_t meniscus = pack565(0xFF, 0xFD, 0xF2);
    for (int y = r.y0; y < r.y1; y++)
    {
        size_t lrow = (size_t)(y - box.y0) * SLIDER_RECT_W + (size_t)(r.x0 - box.x0);
        uint16_t *dst = s.work + (size_t)y * FACE_SIZE + r.x0;
        size_t n = (size_t)(r.x1 - r.x0);
        if (y < cap_y0)
        {
            memcpy(dst, empty + lrow, n * sizeof(uint16_t));
            continue;
        }
        if (y >= cap_y1)
        {
            memcpy(dst, full + lrow, n * sizeof(uint16_t));
            continue;
        }
        const float fy = y + 0.5f;
        const float edge = clamp01(1.0f - fabsf(fy - top - 2.5f) / 2.5f) * 0.55f;
        for (int x = r.x0; x < r.x1; x++)
        {
            size_t k = lrow + (size_t)(x - r.x0);
            dst[x - r.x0] = empty[k];
            float cov = clamp01(0.5f - sd_round_rect(x + 0.5f, fy, ix0, top, ix1, iy1, in_r));
            if (cov > 0.0f)
            {
                blend565_px(&dst[x - r.x0], full[k], (int)(cov * 256.0f));
                if (edge > 0.0f)
                {
                    blend565_px(&dst[x - r.x0], meniscus, (int)(edge * cov * 256.0f));
                }
            }
        }
    }
}

static void icon_stroke(lv_layer_t *layer, lv_color_t color, int x1, int y1, int x2, int y2, int width,
                        bool round)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1 = (lv_point_precise_t){x1, y1};
    dsc.p2 = (lv_point_precise_t){x2, y2};
    dsc.round_start = round;
    dsc.round_end = round;
    if (color.red > 200)
    {
        dsc.color = lv_color_hex(COLOR_GLOW);
        dsc.opa = LV_OPA_10;
        dsc.width = width + 6;
        lv_draw_line(layer, &dsc);
        dsc.opa = LV_OPA_20;
        dsc.width = width + 3;
        lv_draw_line(layer, &dsc);
    }
    dsc.color = color;
    dsc.opa = LV_OPA_COVER;
    dsc.width = width;
    lv_draw_line(layer, &dsc);
}

static void icon_line(lv_layer_t *layer, lv_color_t color, int x1, int y1, int x2, int y2, int width)
{
    icon_stroke(layer, color, x1, y1, x2, y2, width, true);
}

static void icon_arc(lv_layer_t *layer, lv_color_t color, int cx, int cy, int radius, int start, int end,
                     int width)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center = (lv_point_t){cx, cy};
    dsc.start_angle = start;
    dsc.end_angle = end;
    dsc.rounded = true;
    if (color.red > 200)
    {
        dsc.color = lv_color_hex(COLOR_GLOW);
        dsc.opa = LV_OPA_10;
        dsc.width = width + 6;
        dsc.radius = radius + 3;
        lv_draw_arc(layer, &dsc);
        dsc.opa = LV_OPA_20;
        dsc.width = width + 3;
        dsc.radius = radius + 1;
        lv_draw_arc(layer, &dsc);
    }
    dsc.color = color;
    dsc.opa = LV_OPA_COVER;
    dsc.width = width;
    dsc.radius = radius;
    lv_draw_arc(layer, &dsc);
}

static void icon_dot(lv_layer_t *layer, lv_color_t color, int cx, int cy, int radius)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.radius = LV_RADIUS_CIRCLE;
    if (color.red > 200)
    {
        dsc.bg_color = lv_color_hex(COLOR_GLOW);
        dsc.bg_opa = LV_OPA_20;
        lv_area_t glow = {cx - radius - 2, cy - radius - 2, cx + radius + 2, cy + radius + 2};
        lv_draw_rect(layer, &dsc, &glow);
        dsc.bg_color = color;
    }
    dsc.bg_opa = LV_OPA_COVER;
    lv_area_t area = {cx - radius, cy - radius, cx + radius, cy + radius};
    lv_draw_rect(layer, &dsc, &area);
}

static void draw_moon(lv_layer_t *layer, lv_color_t color, int x, int y)
{
    /* Supersampled circle difference: a transparent crescent without triangle seams. */
    static const uint8_t alpha[32 * 32] = {
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        8,
        20,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        16,
        128,
        219,
        20,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        92,
        235,
        255,
        128,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        4,
        155,
        255,
        255,
        247,
        20,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        4,
        171,
        255,
        255,
        255,
        175,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        155,
        255,
        255,
        255,
        255,
        108,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        92,
        255,
        255,
        255,
        255,
        255,
        52,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        16,
        235,
        255,
        255,
        255,
        255,
        255,
        20,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        128,
        255,
        255,
        255,
        255,
        255,
        255,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        8,
        235,
        255,
        255,
        255,
        255,
        255,
        255,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        80,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        20,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        147,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        52,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        203,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        108,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        235,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        175,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        247,
        20,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        128,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        235,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        239,
        20,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        203,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        163,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        147,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        100,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        80,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        251,
        84,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        8,
        235,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        251,
        100,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        128,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        163,
        20,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        16,
        235,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        239,
        128,
        20,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        92,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        247,
        175,
        108,
        52,
        20,
        0,
        0,
        20,
        52,
        108,
        32,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        155,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        155,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        4,
        171,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        171,
        4,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        4,
        155,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        155,
        4,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        92,
        235,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        235,
        92,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        16,
        128,
        235,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        235,
        128,
        16,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        8,
        80,
        147,
        203,
        235,
        255,
        255,
        235,
        203,
        147,
        80,
        8,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    };
    static const lv_image_dsc_t moon = {
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = 32, .h = 32, .stride = 32},
        .data_size = sizeof(alpha),
        .data = alpha,
    };
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = &moon;
    dsc.recolor_opa = LV_OPA_COVER;
    lv_area_t area = {x - 16, y - 16, x + 15, y + 15};
    static const int8_t offsets[][2] = {
        {-2, 0},
        {2, 0},
        {0, -2},
        {0, 2},
        {-2, -2},
        {2, -2},
        {-2, 2},
        {2, 2},
    };
    dsc.recolor = lv_color_hex(COLOR_GLOW);
    dsc.opa = LV_OPA_10;
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
    {
        lv_area_t glow = {area.x1 + offsets[i][0], area.y1 + offsets[i][1], area.x2 + offsets[i][0],
                          area.y2 + offsets[i][1]};
        lv_draw_image(layer, &dsc, &glow);
    }
    dsc.recolor = color;
    dsc.opa = LV_OPA_COVER;
    lv_draw_image(layer, &dsc, &area);
}

static void draw_icon(lv_layer_t *layer, settings_icon_t icon, int x, int y, lv_color_t color)
{
    switch (icon)
    {
    case ICON_WIFI:
        icon_arc(layer, color, x, y + 10, 24, 225, 315, 4);
        icon_arc(layer, color, x, y + 10, 16, 225, 315, 4);
        icon_arc(layer, color, x, y + 10, 8, 225, 315, 4);
        icon_dot(layer, color, x, y + 10, 3);
        break;
    case ICON_PORTAL: /* a small globe */
        icon_arc(layer, color, x, y, 16, 0, 359, 3);
        icon_line(layer, color, x - 16, y, x + 16, y, 3);
        icon_line(layer, color, x, y - 16, x, y + 16, 3);
        icon_arc(layer, color, x + 11, y, 15, 148, 212, 3);
        icon_arc(layer, color, x - 11, y, 15, 328, 32, 3);
        break;
    case ICON_MOON:
        draw_moon(layer, color, x, y);
        break;
    case ICON_SLEEP:
    {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = lv_color_hex(COLOR_GLOW);
        dsc.bg_opa = LV_OPA_20;
        dsc.radius = 12;
        lv_area_t glow = {x - 20, y - 10, x + 20, y + 16};
        lv_draw_rect(layer, &dsc, &glow);
        dsc.bg_color = color;
        dsc.bg_opa = LV_OPA_COVER;
        dsc.radius = 10;
        lv_area_t head = {x - 18, y - 8, x + 18, y + 14};
        lv_draw_rect(layer, &dsc, &head);
        dsc.bg_color = lv_color_hex(0x301506);
        dsc.radius = 8;
        lv_area_t face = {x - 13, y - 4, x + 13, y + 10};
        lv_draw_rect(layer, &dsc, &face);
        dsc.bg_color = color;
        dsc.radius = LV_RADIUS_CIRCLE;
        lv_area_t left_eye = {x - 8, y, x - 4, y + 4};
        lv_area_t right_eye = {x + 4, y, x + 8, y + 4};
        lv_draw_rect(layer, &dsc, &left_eye);
        lv_draw_rect(layer, &dsc, &right_eye);
        icon_line(layer, color, x, y - 9, x, y - 15, 3);
        icon_dot(layer, color, x, y - 18, 3);
        break;
    }
    case ICON_PALETTE:
        icon_arc(layer, color, x, y, 15, 75, 15, 3);
        icon_arc(layer, color, x + 9, y + 9, 4, 0, 360, 3);
        icon_dot(layer, color, x - 7, y - 5, 3);
        icon_dot(layer, color, x + 2, y - 9, 3);
        icon_dot(layer, color, x + 9, y - 3, 3);
        icon_dot(layer, color, x - 7, y + 5, 3);
        break;
    case ICON_GEAR:
        for (int k = 0; k < 8; k++)
        {
            float a = k * 3.14159265f / 4.0f + 3.14159265f / 8.0f;
            int c = (int)lroundf(cosf(a) * 10.0f), sn = (int)lroundf(sinf(a) * 10.0f);
            int c2 = (int)lroundf(cosf(a) * 16.0f), s2 = (int)lroundf(sinf(a) * 16.0f);
            icon_stroke(layer, color, x + c, y + sn, x + c2, y + s2, 6, false);
        }
        icon_arc(layer, color, x, y, 9, 0, 360, 6);
        break;
    case ICON_MORE:
        icon_dot(layer, color, x - 11, y, 3);
        icon_dot(layer, color, x, y, 3);
        icon_dot(layer, color, x + 11, y, 3);
        break;
    case ICON_BACK:
        icon_line(layer, color, x + 4, y - 10, x - 6, y, 4);
        icon_line(layer, color, x - 6, y, x + 4, y + 10, 4);
        break;
    case ICON_SUN:
    {
        icon_dot(layer, color, x, y, 6);
        static const int8_t rays[][4] = {
            {0, -11, 0, -15},
            {0, 11, 0, 15},
            {-11, 0, -15, 0},
            {11, 0, 15, 0},
            {-8, -8, -11, -11},
            {8, -8, 11, -11},
            {-8, 8, -11, 11},
            {8, 8, 11, 11},
        };
        for (size_t k = 0; k < sizeof(rays) / sizeof(rays[0]); k++)
        {
            icon_line(layer, color, x + rays[k][0], y + rays[k][1], x + rays[k][2], y + rays[k][3], 2);
        }
        break;
    }
    case ICON_UPDATE: /* an arrow dropping into a tray */
        icon_line(layer, color, x, y - 13, x, y + 5, 4);
        icon_line(layer, color, x - 8, y - 2, x, y + 7, 4);
        icon_line(layer, color, x + 8, y - 2, x, y + 7, 4);
        icon_line(layer, color, x - 13, y + 12, x + 13, y + 12, 4);
        icon_line(layer, color, x - 13, y + 7, x - 13, y + 12, 3);
        icon_line(layer, color, x + 13, y + 7, x + 13, y + 12, 3);
        break;
    case ICON_EYES: /* two round eyes */
        icon_arc(layer, color, x - 10, y, 8, 0, 359, 3);
        icon_arc(layer, color, x + 10, y, 8, 0, 359, 3);
        icon_dot(layer, color, x - 10, y + 2, 3);
        icon_dot(layer, color, x + 10, y + 2, 3);
        break;
    case ICON_SQUARE: /* the squircle style */
        icon_arc(layer, color, x - 7, y - 7, 6, 180, 270, 3);
        icon_arc(layer, color, x + 7, y - 7, 6, 270, 360, 3);
        icon_arc(layer, color, x + 7, y + 7, 6, 0, 90, 3);
        icon_arc(layer, color, x - 7, y + 7, 6, 90, 180, 3);
        icon_line(layer, color, x - 7, y - 13, x + 7, y - 13, 3);
        icon_line(layer, color, x - 7, y + 13, x + 7, y + 13, 3);
        icon_line(layer, color, x - 13, y - 7, x - 13, y + 7, 3);
        icon_line(layer, color, x + 13, y - 7, x + 13, y + 7, 3);
        break;
    case ICON_NEXT:
        icon_line(layer, color, x - 4, y - 10, x + 6, y, 4);
        icon_line(layer, color, x + 6, y, x - 4, y + 10, 4);
        break;
    case ICON_AUTO: /* a clock face: it updates on its own */
        icon_arc(layer, color, x, y, 15, 0, 359, 3);
        icon_line(layer, color, x, y, x, y - 9, 3);
        icon_line(layer, color, x, y, x + 7, y + 4, 3);
        break;
    case ICON_VOLUME:
        icon_line(layer, color, x - 11, y - 4, x - 11, y + 4, 6);
        icon_line(layer, color, x - 7, y - 4, x - 2, y - 9, 4);
        icon_line(layer, color, x - 7, y + 4, x - 2, y + 9, 4);
        icon_line(layer, color, x - 2, y - 9, x - 2, y + 9, 4);
        if (s.value[1] == 0)
        {
            icon_line(layer, color, x + 5, y - 5, x + 12, y + 5, 3);
            icon_line(layer, color, x + 12, y - 5, x + 5, y + 5, 3);
        }
        else
        {
            icon_arc(layer, color, x, y, 9, 310, 50, 3);
            if (s.value[1] > 50)
            {
                icon_arc(layer, color, x, y, 15, 310, 50, 3);
            }
        }
        break;
    }
}

static void draw_text(lv_layer_t *layer, const char *text, const lv_font_t *font, int x0, int x1, int cy,
                      lv_text_align_t align, uint32_t color, lv_opa_t opa)
{
    int lh = lv_font_get_line_height(font);
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = font;
    dsc.align = align;
    dsc.color = lv_color_hex(color);
    dsc.opa = opa;
    dsc.text_local = 1; /* drawn when the layer finishes, after callers' buffers are gone */
    lv_area_t area = {x0, cy - lh / 2, x1 - 1, cy - lh / 2 + lh - 1};
    lv_draw_label(layer, &dsc, &area);
}

/* Soft amber halo: a few dim offset copies under the crisp text, like the clock digits. */
static void draw_glow_text(lv_layer_t *layer, const char *text, const lv_font_t *font, int cx, int cy, int half_w)
{
    static const int8_t offsets[][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
    {
        draw_text(layer, text, font, cx - half_w + offsets[i][0], cx + half_w + offsets[i][0], cy + offsets[i][1],
                  LV_TEXT_ALIGN_CENTER, COLOR_PRIMARY, LV_OPA_30);
    }
    draw_text(layer, text, font, cx - half_w, cx + half_w, cy, LV_TEXT_ALIGN_CENTER, COLOR_TEXT, LV_OPA_COVER);
}

/* Shortens `text` in place with "..." so it fits in max_width pixels. */
static void fit_text(char *text, size_t size, const lv_font_t *font, int max_width)
{
    lv_point_t sz;
    lv_text_get_size(&sz, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (sz.x <= max_width || size < 4)
    {
        return;
    }
    size_t len = LV_MIN(strlen(text), size - 4);
    while (len > 0)
    {
        len--;
        memcpy(text + len, "...", 4);
        lv_text_get_size(&sz, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= max_width)
        {
            return;
        }
    }
}

static void draw_button(lv_layer_t *layer, int id)
{
    const button_def_t *b = button_def(id);
    float press = s.press[id];
    float scale = (float)b->size / 66.0f;
    int r = (int)lroundf(32.0f * scale);
    float glow = LV_MAX(s.active[id], press);

    /* When clicked or active, draw a radiant amber ambient halo around the button */
    if (glow > 0.02f)
    {
        lv_draw_rect_dsc_t halo;
        lv_draw_rect_dsc_init(&halo);
        halo.bg_color = lv_color_hex(COLOR_GLOW);
        halo.radius = LV_RADIUS_CIRCLE;
        halo.bg_opa = (lv_opa_t)lroundf(0.35f * glow * 255.0f);
        lv_area_t halo_area = {b->cx - r - 6, b->cy - r - 6, b->cx + r + 5, b->cy + r + 5};
        lv_draw_rect(layer, &halo, &halo_area);

        halo.bg_color = lv_color_hex(COLOR_PRIMARY);
        halo.bg_opa = (lv_opa_t)lroundf(0.20f * glow * 255.0f);
        lv_area_t halo_mid = {b->cx - r - 3, b->cy - r - 3, b->cx + r + 2, b->cy + r + 2};
        lv_draw_rect(layer, &halo, &halo_mid);
    }

    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.scale_x = dsc.scale_y = (int32_t)lroundf(scale * LV_SCALE_NONE);
    dsc.pivot = (lv_point_t){43, 43};
    dsc.antialias = 1;
    lv_area_t area = {b->cx - 43, b->cy - 43, b->cx + 42, b->cy + 42};
    const lv_image_dsc_t *normal = images_get(IMAGE_SETTINGS_BUTTON);
    const lv_image_dsc_t *lit = images_get(IMAGE_SETTINGS_BUTTON_ACTIVE);
    if (normal && glow < 0.99f)
    {
        dsc.src = normal;
        dsc.opa = LV_OPA_COVER;
        lv_draw_image(layer, &dsc, &area);
    }
    if (lit && glow > 0.01f)
    {
        dsc.src = lit;
        dsc.opa = (lv_opa_t)lroundf(glow * 255.0f);
        lv_draw_image(layer, &dsc, &area);
    }
    if (!normal && !lit)
    {
        lv_draw_rect_dsc_t rect;
        lv_draw_rect_dsc_init(&rect);
        rect.bg_color = lv_color_hex(0x21150F);
        rect.radius = LV_RADIUS_CIRCLE;
        rect.border_color = lv_color_hex(0xA94C12);
        rect.border_width = 2;
        lv_area_t circle = {b->cx - r, b->cy - r, b->cx + r, b->cy + r};
        lv_draw_rect(layer, &rect, &circle);
    }

    /* Active / clicked state: warm glowing amber face and illuminated gold rim */
    if (glow > 0.02f)
    {
        lv_draw_rect_dsc_t face;
        lv_draw_rect_dsc_init(&face);
        face.bg_color = lv_color_hex(COLOR_PRIMARY);
        face.bg_opa = (lv_opa_t)lroundf(0.42f * glow * 255.0f);
        face.radius = LV_RADIUS_CIRCLE;
        lv_area_t inner = {b->cx - r + 3, b->cy - r + 3, b->cx + r - 3, b->cy + r - 3};
        lv_draw_rect(layer, &face, &inner);

        lv_draw_rect_dsc_t rim;
        lv_draw_rect_dsc_init(&rim);
        rim.bg_opa = LV_OPA_TRANSP;
        rim.radius = LV_RADIUS_CIRCLE;
        rim.border_color = lv_color_hex(COLOR_SECONDARY);
        rim.border_width = 2;
        rim.border_opa = (lv_opa_t)lroundf(0.85f * glow * 255.0f);
        lv_area_t rim_area = {b->cx - r, b->cy - r, b->cx + r - 1, b->cy + r - 1};
        lv_draw_rect(layer, &rim, &rim_area);
    }

    uint32_t icon_color = glow > 0.5f ? 0xFFFFFF : COLOR_TEXT;
    draw_icon(layer, b->icon, b->cx, b->cy, lv_color_hex(icon_color));
}

static void draw_caption(lv_layer_t *layer, int id)
{
    const button_def_t *b = button_def(id);
    if ((s.page == PAGE_WIFI || s.page == PAGE_PORTAL) && s.info.wifi_qr[0] && id == BTN_BACK) return;
    if (!b->caption[0]) return;
    draw_text(layer, b->caption, &fredoka_medium_22, b->cx - 72, b->cx + 72, b->cy + b->size / 2 + 13,
              LV_TEXT_ALIGN_CENTER, COLOR_TEXT, LV_OPA_COVER);
}

static void draw_slider_overlay(lv_layer_t *layer, int i)
{
    int cx = SLIDER_CX[i];
    int ky = SLIDER_BOTTOM - SLIDER_INSET - 2 - KNOB_R;
    draw_icon(layer, i == 0 ? ICON_SUN : ICON_VOLUME, cx, ky, lv_color_hex(COLOR_ON_CREAM));
    if (s.label_alpha[i] > 0.01f)
    {
        char text[8];
        snprintf(text, sizeof(text), "%d", s.value[i]);
        int cy = SLIDER_TOP + 22;
        bool on_fill = fill_top(i) < cy - 6;
        draw_text(layer, text, &fredoka_medium_22, cx - 26, cx + 26, cy, LV_TEXT_ALIGN_CENTER,
                  on_fill ? COLOR_ON_CREAM : COLOR_TEXT, (lv_opa_t)lroundf(s.label_alpha[i] * 255.0f));
    }
}

/* A quiet maker signature in the same amber palette as the clock. */
static void draw_maker_credit(lv_layer_t *layer, int top)
{
    lv_draw_rect_dsc_t card;
    lv_draw_rect_dsc_init(&card);
    card.radius = 18;
    card.bg_color = lv_color_hex(0x291A12);
    card.bg_opa = LV_OPA_70;
    card.border_color = lv_color_hex(COLOR_PRIMARY);
    card.border_opa = LV_OPA_30;
    card.border_width = 1;
    lv_area_t area = {ROW_LEFT, top, ROW_RIGHT-1, top+107};
    lv_draw_rect(layer, &card, &area);

    /* Draw the heart directly so it doesn't depend on a font's symbol coverage. */
    lv_draw_rect_dsc_t heart;
    lv_draw_rect_dsc_init(&heart);
    heart.radius = LV_RADIUS_CIRCLE;
    heart.bg_color = lv_color_hex(0xFF9B79);
    heart.bg_opa = LV_OPA_COVER;
    lv_area_t left = {222, top+14, 234, top+26};
    lv_area_t right = {232, top+14, 244, top+26};
    lv_draw_rect(layer, &heart, &left);
    lv_draw_rect(layer, &heart, &right);
    lv_draw_triangle_dsc_t tip;
    lv_draw_triangle_dsc_init(&tip);
    tip.color = heart.bg_color;
    tip.opa = LV_OPA_COVER;
    tip.p[0] = (lv_point_precise_t){223,top+23};
    tip.p[1] = (lv_point_precise_t){243,top+23};
    tip.p[2] = (lv_point_precise_t){233,top+36};
    lv_draw_triangle(layer, &tip);

    draw_text(layer, "Made with love by", &fredoka_medium_16, ROW_LEFT, ROW_RIGHT,
              top+53, LV_TEXT_ALIGN_CENTER, COLOR_SECONDARY, LV_OPA_90);
    draw_glow_text(layer, "Arya Aghaei", &fredoka_medium_28, 233, top+82, 140);
}

static void draw_eyes_page(lv_layer_t *layer)
{
    int count = eyes_themes_count();
    int selected = s.controls.get_eyes_theme ? s.controls.get_eyes_theme() : 0;
    const eyes_theme_t *theme = eyes_themes_get(selected);
    int style = s.controls.get_eyes_style ? s.controls.get_eyes_style() : 0;
    ui_draw_eye(layer, 233 - 56, 178, 46, theme, style, false, false);
    ui_draw_eye(layer, 233 + 56, 178, 46, theme, style, false, false);
    draw_text(layer, eyes_themes_name(selected), &fredoka_medium_36, 70, 396, 278, LV_TEXT_ALIGN_CENTER,
              COLOR_TEXT, LV_OPA_COVER);
    int spacing = 22, x0 = 233 - (count - 1) * spacing / 2;
    for (int i = 0; i < count; i++)
    {
        lv_color_t c = lv_color_hex(eyes_themes_get(i)->fill_bottom);
        int x = x0 + i * spacing, r = i == selected ? 7 : 4;
        icon_dot(layer, c, x, 318, r);
    }
}

static void draw_info(lv_layer_t *layer)
{
    if (s.page == PAGE_EYES)
    {
        draw_eyes_page(layer);
        return;
    }
    const info_t *info = &s.info;
    if (s.confirm_setup) {
        draw_text(layer, "Set up Wi-Fi again?", &fredoka_medium_26, 48, 418, 178,
                  LV_TEXT_ALIGN_CENTER, COLOR_TEXT, LV_OPA_COVER);
        draw_text(layer, "Your saved network stays", &fredoka_medium_22, 48, 418, 228,
                  LV_TEXT_ALIGN_CENTER, COLOR_SECONDARY, LV_OPA_COVER);
        draw_text(layer, "until a new one connects.", &fredoka_medium_22, 48, 418, 258,
                  LV_TEXT_ALIGN_CENTER, COLOR_SECONDARY, LV_OPA_COVER);
        return;
    }
    if (info->wifi_qr[0]) {
        if (info->wifi_stage == 3 || info->wifi_stage == 4) {
            bool complete = info->wifi_stage == 4;
            if (complete) {
                icon_line(layer, lv_color_hex(COLOR_SECONDARY), 192, 210, 222, 240, 7);
                icon_line(layer, lv_color_hex(COLOR_SECONDARY), 222, 240, 276, 182, 7);
            } else {
                draw_icon(layer, ICON_WIFI, 233, 212, lv_color_hex(COLOR_TEXT));
            }
            draw_text(layer, complete ? "You're all set" : "Joining your network...", &fredoka_medium_26,
                      58, 408, 292, LV_TEXT_ALIGN_CENTER, COLOR_TEXT, LV_OPA_COVER);
            draw_text(layer, complete ? "Return to your usual Wi-Fi" : "Keep your phone connected", &fredoka_medium_22,
                      48, 418, 332, LV_TEXT_ALIGN_CENTER, COLOR_SECONDARY, LV_OPA_COVER);
            return;
        }
        /* Compact white margin like the reference; keep QR modules square.
         * Both payloads are checked against an independent image decoder. */
        static char payload[128];
        static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
        static bool valid;
        if (strcmp(payload, info->wifi_qr)) {
            uint8_t work[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
            valid = qrcodegen_encodeText(info->wifi_qr, work, qr, qrcodegen_Ecc_MEDIUM,
                                         1, 6, qrcodegen_Mask_AUTO, true);
            snprintf(payload, sizeof(payload), "%s", info->wifi_qr);
        }
        bool phone_portal = info->wifi_stage == 5;
        bool portal = info->wifi_stage == 2 || phone_portal;
        uint32_t accent = portal ? COLOR_PRIMARY : COLOR_SECONDARY;
        lv_draw_rect_dsc_t ink;
        lv_draw_rect_dsc_init(&ink);
        ink.bg_color = lv_color_white();
        ink.bg_opa = LV_OPA_COVER;
        ink.border_color = lv_color_hex(accent);
        ink.border_width = 2;
        ink.radius = 10;
        int modules = valid ? qrcodegen_getSize(qr) : 0;
        int scale = valid ? 252 / (modules + 4) : 1;
        int size = valid ? (modules + 4) * scale : 200;
        int x0 = 233 - size / 2, y0 = 222 - size / 2;
        /* The encoded quiet zone already supplies the white border; add only
         * two pixels for the outline instead of padding a fixed-size card. */
        lv_area_t card = {x0 - 2, y0 - 2, x0 + size + 1, y0 + size + 1};
        lv_draw_rect(layer, &ink, &card);
        if (valid) {
            ink.border_width = 0;
            ink.radius = 0;
            ink.bg_color = lv_color_black();
            for (int y = 0; y < modules; y++) {
                for (int x = 0; x < modules; x++) {
                    if (!qrcodegen_getModule(qr, x, y)) continue;
                    lv_area_t area = {x0 + (x + 2) * scale, y0 + (y + 2) * scale,
                                     x0 + (x + 3) * scale - 1, y0 + (y + 3) * scale - 1};
                    lv_draw_rect(layer, &ink, &area);
                }
            }
        }
        draw_text(layer, phone_portal ? "Scan to open and pair" : portal ? "Wi-Fi joined • Scan this new code" : info->setup_ssid,
                  &fredoka_medium_22, 40, 426, 375, LV_TEXT_ALIGN_CENTER, accent, LV_OPA_COVER);
        char password[32];
        snprintf(password, sizeof(password), "Password: %s", info->setup_password);
        draw_text(layer, phone_portal ? info->setup_ssid : portal ? "http://192.168.4.1/" : password,
                  &fredoka_medium_16, 58, 408, 398, LV_TEXT_ALIGN_CENTER, COLOR_TEXT, LV_OPA_COVER);
        return;
    }
    int rows_h = info->count * ROW_HEIGHT;
    int note_h = info->note[0] ? (s.page == PAGE_ABOUT ? 96 : 64) : 0;
    int y = (BODY_Y0 + BODY_Y1) / 2 - (rows_h + note_h) / 2;
    if (s.page == PAGE_WIFI) y = 126;
    if (s.page == PAGE_ABOUT) y = BODY_Y0 - s.info_scroll;
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(COLOR_PRIMARY);
    line.opa = LV_OPA_30;
    line.width = 1;
    for (int k = 0; k < info->count; k++)
    {
        int cy = y + k * ROW_HEIGHT + ROW_HEIGHT / 2;
        char value[sizeof(info->rows[k].value)];
        snprintf(value, sizeof(value), "%s", info->rows[k].value);
        fit_text(value, sizeof(value), &fredoka_medium_22, 160);
        draw_text(layer, info->rows[k].label, &fredoka_medium_22, ROW_LEFT, ROW_RIGHT, cy, LV_TEXT_ALIGN_LEFT,
                  COLOR_SECONDARY, LV_OPA_80);
        draw_text(layer, value, &fredoka_medium_22, ROW_LEFT, ROW_RIGHT, cy, LV_TEXT_ALIGN_RIGHT, COLOR_TEXT,
                  LV_OPA_COVER);
        if (k + 1 < info->count)
        {
            line.p1 = (lv_point_precise_t){ROW_LEFT, y + (k + 1) * ROW_HEIGHT};
            line.p2 = (lv_point_precise_t){ROW_RIGHT, y + (k + 1) * ROW_HEIGHT};
            lv_draw_line(layer, &line);
        }
    }
    if (info->note[0])
    {
        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.text = info->note;
        dsc.font = &fredoka_medium_22;
        dsc.align = LV_TEXT_ALIGN_CENTER;
        dsc.color = lv_color_hex(COLOR_SECONDARY);
        dsc.line_space = 3;
        lv_area_t area = {ROW_LEFT + 4, y + rows_h + 12, ROW_RIGHT - 4, y + rows_h + note_h + 12};
        lv_draw_label(layer, &dsc, &area);
    }
    if (s.page == PAGE_ABOUT)
        draw_maker_credit(layer, y + rows_h + 116);
}

static rect_t spinner_rect(int angle)
{
    rect_t r = {FACE_SIZE, FACE_SIZE, 0, 0};
    for (int a = angle; a <= angle + 90; a += 5) {
        float rad = a * 3.14159265f / 180.0f;
        int x = (int)lroundf(233 + 226 * cosf(rad));
        int y = (int)lroundf(233 + 226 * sinf(rad));
        r.x0 = LV_MIN(r.x0, x - 5); r.y0 = LV_MIN(r.y0, y - 5);
        r.x1 = LV_MAX(r.x1, x + 6); r.y1 = LV_MAX(r.y1, y + 6);
    }
    return r;
}

/* Recomposes one upright rectangle of the current page into `work`. */
static void draw_scene(rect_t r)
{
    draw_background(r);
    bool main_page = s.page == PAGE_MAIN;
    if (main_page)
    {
        for (int i = 0; i < 2; i++)
        {
            draw_slider_body(i, r);
        }
    }
    lv_layer_t layer;
    lv_canvas_init_layer(s.draw_canvas, &layer);
    layer._clip_area = (lv_area_t){r.x0, r.y0, r.x1 - 1, r.y1 - 1};
    layer.phy_clip_area = layer._clip_area;
    rect_t title = {80, 34, 386, 86};
    if (rect_overlaps(r, title))
    {
        draw_glow_text(&layer, page_title(s.page), &fredoka_medium_36, 233, 60, 150);
    }
    if (main_page)
    {
        for (int i = 0; i < 2; i++)
        {
            if (rect_overlaps(r, slider_rect(i)))
            {
                draw_slider_overlay(&layer, i);
            }
        }
    }
    else if (rect_overlaps(r, body_rect()) || ((s.page == PAGE_WIFI || s.page == PAGE_PORTAL) && s.info.wifi_qr[0]))
    {
        lv_area_t saved = layer._clip_area;
        if (s.page == PAGE_ABOUT) {
            layer._clip_area.x1 = LV_MAX(saved.x1, BODY_X0);
            layer._clip_area.x2 = LV_MIN(saved.x2, BODY_X1 - 1);
            layer._clip_area.y1 = LV_MAX(saved.y1, BODY_Y0);
            layer._clip_area.y2 = LV_MIN(saved.y2, BODY_Y1 - 1);
            layer.phy_clip_area = layer._clip_area;
        }
        draw_info(&layer);
        if (s.page == PAGE_ABOUT) {
            int content_h = s.info.count * ROW_HEIGHT + ABOUT_FOOTER_H;
            int viewport_h = BODY_Y1 - BODY_Y0;
            int thumb_h = viewport_h * viewport_h / content_h;
            int top = BODY_Y0 + s.info_scroll * (viewport_h - thumb_h) / (content_h - viewport_h);
            lv_draw_rect_dsc_t bar;
            lv_draw_rect_dsc_init(&bar);
            bar.bg_color = lv_color_hex(COLOR_SECONDARY);
            bar.bg_opa = LV_OPA_80;
            bar.radius = 2;
            lv_area_t thumb = {BODY_X1 - 4, top, BODY_X1 - 2, top + thumb_h - 1};
            lv_draw_rect(&layer, &bar, &thumb);
        }
        layer._clip_area = saved;
        layer.phy_clip_area = saved;
    }
    for (int id = 0; id < BTN_COUNT; id++)
    {
        if (!button_on_page(id))
        {
            continue;
        }
        if (rect_overlaps(r, button_rect(id)))
        {
            draw_button(&layer, id);
        }
        const button_def_t *b = button_def(id);
        rect_t caption = {b->cx - 72, b->cy + b->size / 2, b->cx + 72, b->cy + b->size / 2 + 34};
        if (rect_overlaps(r, caption))
        {
            draw_caption(&layer, id);
        }
    }
    if (s.page == PAGE_WIFI && s.info.wifi_stage == 3) {
        lv_draw_arc_dsc_t ring;
        lv_draw_arc_dsc_init(&ring);
        ring.center = (lv_point_t){233, 233};
        ring.radius = 226;
        ring.width = 5;
        ring.rounded = true;
        ring.color = lv_color_hex(COLOR_SECONDARY);
        ring.start_angle = s.spinner_angle;
        ring.end_angle = s.spinner_angle + 90;
        lv_draw_arc(&layer, &ring);
    }
    lv_canvas_finish_layer(s.draw_canvas, &layer);
}

/* Panel index of upright (x, y) = ROT_ORIGIN + x * ROT_STEP_X + y * ROT_STEP_Y. */
#if UI_ROTATION_DEG == 90
#define ROT_ORIGIN (FACE_SIZE - 1)
#define ROT_STEP_X FACE_SIZE
#define ROT_STEP_Y (-1)
#elif UI_ROTATION_DEG == 180
#define ROT_ORIGIN (FACE_SIZE * FACE_SIZE - 1)
#define ROT_STEP_X (-1)
#define ROT_STEP_Y (-FACE_SIZE)
#elif UI_ROTATION_DEG == 270
#define ROT_ORIGIN ((FACE_SIZE - 1) * FACE_SIZE)
#define ROT_STEP_X (-FACE_SIZE)
#define ROT_STEP_Y 1
#else
#define ROT_ORIGIN 0
#define ROT_STEP_X 1
#define ROT_STEP_Y FACE_SIZE
#endif

typedef struct
{
    rect_t r;
} rotate_job_t;

/* Walked in 32 px tiles so both the PSRAM reads and the strided writes stay cache-friendly. */
static void rotate_rows(void *arg)
{
    const rect_t r = ((const rotate_job_t *)arg)->r;
    for (int ty = r.y0; ty < r.y1; ty += 32)
    {
        int ye = LV_MIN(ty + 32, r.y1);
        for (int tx = r.x0; tx < r.x1; tx += 32)
        {
            int xe = LV_MIN(tx + 32, r.x1);
            for (int y = ty; y < ye; y++)
            {
                const uint16_t *src = s.work + (size_t)y * FACE_SIZE + tx;
                uint16_t *dst = s.panel + ROT_ORIGIN + tx * ROT_STEP_X + y * ROT_STEP_Y;
                for (int x = tx; x < xe; x++)
                {
                    *dst = *src++;
                    dst += ROT_STEP_X;
                }
            }
        }
    }
}

/* Copies an upright rectangle into the panel-ordered frame (large areas on both cores) and
 * returns the panel area. */
static rect_t rotate_rect(rect_t r)
{
    if ((r.x1 - r.x0) * (r.y1 - r.y0) > 32768)
    {
        int mid = r.y0 + ((r.y1 - r.y0) / 2 & ~31);
        rotate_job_t helper = {{r.x0, mid, r.x1, r.y1}};
        rotate_job_t own = {{r.x0, r.y0, r.x1, mid}};
        eyes_panel_run_async(rotate_rows, &helper);
        rotate_rows(&own);
        eyes_panel_join();
    }
    else
    {
        rotate_job_t job = {r};
        rotate_rows(&job);
    }
    int ax, ay, bx, by;
    to_panel(r.x0, r.y0, &ax, &ay);
    to_panel(r.x1 - 1, r.y1 - 1, &bx, &by);
    return (rect_t){LV_MIN(ax, bx), LV_MIN(ay, by), LV_MAX(ax, bx) + 1, LV_MAX(ay, by) + 1};
}

static void present(rect_t panel_area)
{
    if (s.direct)
    {
        ui_panel_send(&s.strips, s.panel, panel_area.x0, panel_area.y0, panel_area.x1, panel_area.y1);
    }
    else if (s.canvas)
    {
        lv_area_t area = {panel_area.x0, panel_area.y0, panel_area.x1 - 1, panel_area.y1 - 1};
        lv_obj_invalidate_area(s.canvas, &area);
    }
}

static void render(rect_t r)
{
    rect_t full = {0, 0, FACE_SIZE, FACE_SIZE};
    if (!rect_intersect(&r, r, full))
    {
        return;
    }
    draw_scene(r);
    rect_t panel_area = rotate_rect(r);
    if (!s.page_fading)
    {
        present(panel_area);
    }
}

static bool ensure_button_cache(int id)
{
    rect_t r = button_rect(id);
    size_t width = (size_t)(r.x1 - r.x0), height = (size_t)(r.y1 - r.y0);
    size_t pixels = width * height;
    if (s_cache.button_valid[id] && s_cache.button_pixels[id][0] >= pixels &&
        s_cache.button_pixels[id][1] >= pixels) {
        return true;
    }
    s_cache.button_valid[id] = false;
    for (int lit = 0; lit < 2; lit++) {
        if (s_cache.button[id][lit] && s_cache.button_pixels[id][lit] < pixels) {
            free_frame(s_cache.button[id][lit]);
            s_cache.button[id][lit] = NULL;
            s_cache.button_pixels[id][lit] = 0;
        }
        if (!s_cache.button[id][lit]) {
            s_cache.button[id][lit] = alloc_frame(pixels * sizeof(uint16_t));
            if (!s_cache.button[id][lit]) return false;
            s_cache.button_pixels[id][lit] = pixels;
        }
    }
    float press = s.press[id], active = s.active[id];
    for (int lit = 0; lit < 2; lit++)
    {
        s.press[id] = 0.0f;
        s.active[id] = (float)lit;
        draw_scene(r);
        for (size_t y = 0; y < height; y++)
        {
            memcpy(s_cache.button[id][lit] + y * width, s.work + (size_t)(r.y0 + (int)y) * FACE_SIZE + r.x0,
                   width * sizeof(uint16_t));
        }
    }
    s.press[id] = press;
    s.active[id] = active;
    s_cache.button_valid[id] = true;
    return true;
}

/* Press and toggle animations mix the button's resting and lit layers instead of redrawing it. */
static void compose_button(int id)
{
    rect_t r = button_rect(id);
    if (!ensure_button_cache(id))
    {
        draw_scene(r);
        return;
    }
    int alpha = (int)(LV_MAX(s.active[id], s.press[id]) * 256.0f);
    size_t width = (size_t)(r.x1 - r.x0);
    for (int y = r.y0; y < r.y1; y++)
    {
        const uint16_t *rest = s_cache.button[id][0] + (size_t)(y - r.y0) * width;
        const uint16_t *lit = s_cache.button[id][1] + (size_t)(y - r.y0) * width;
        uint16_t *dst = s.work + (size_t)y * FACE_SIZE + r.x0;
        memcpy(dst, rest, width * sizeof(uint16_t));
        if (alpha > 0)
        {
            for (size_t x = 0; x < width; x++)
            {
                if (rest[x] != lit[x])
                {
                    blend565_px(&dst[x], lit[x], alpha);
                }
            }
        }
    }
}

static void render_button(int id)
{
    compose_button(id);
    rect_t panel_area = rotate_rect(button_rect(id));
    if (!s.page_fading)
    {
        present(panel_area);
    }
}

/* Composes the whole current page into `panel` without sending it. */
static void compose_page(void)
{
    const rect_t full = {0, 0, FACE_SIZE, FACE_SIZE};
    const size_t size = (size_t)FACE_SIZE * FACE_SIZE * sizeof(uint16_t);
    if (s.page == PAGE_MAIN && s_cache.main)
    {
        memcpy(s.work, s_cache.main, size);
        for (int i = 0; i < 2; i++)
        {
            draw_scene(slider_dirty_rect(i));
        }
        compose_button(BTN_DND);
    }
    else
    {
        draw_scene(full);
        if (s.page == PAGE_MAIN)
        {
            s_cache.main = alloc_frame(size);
            if (s_cache.main)
            {
                memcpy(s_cache.main, s.work, size);
            }
        }
    }
    rotate_rect(full);
}

static void mark_dirty(rect_t r)
{
    for (int k = 0; k < s.dirty_count; k++)
    {
        rect_t *d = &s.dirty[k];
        if (rect_overlaps(*d, r))
        {
            d->x0 = LV_MIN(d->x0, r.x0);
            d->y0 = LV_MIN(d->y0, r.y0);
            d->x1 = LV_MAX(d->x1, r.x1);
            d->y1 = LV_MAX(d->y1, r.y1);
            return;
        }
    }
    if (s.dirty_count < (int)(sizeof(s.dirty) / sizeof(s.dirty[0])))
    {
        s.dirty[s.dirty_count++] = r;
    }
    else
    {
        s.dirty[0] = (rect_t){0, 0, FACE_SIZE, FACE_SIZE};
        s.dirty_count = 1;
    }
}

static void flush_dirty(void)
{
    for (int k = 0; k < s.dirty_count; k++)
    {
        render(s.dirty[k]);
    }
    s.dirty_count = 0;
}

/* ---------------------------------------------------------------- animation */

static void approach(float *value, float target, float dt, float tau)
{
    *value += (target - *value) * (1.0f - expf(-dt / tau));
    if (fabsf(target - *value) < 0.002f)
    {
        *value = target;
    }
}

static bool page_has_live_info(void)
{
    return s.page == PAGE_WIFI || s.page == PAGE_PORTAL || s.page == PAGE_ABOUT || s.page == PAGE_UPDATE;
}

static void apply_slider(int i, bool commit)
{
    if (i == 0)
    {
        s.controls.set_brightness(s.value[0], commit);
    }
    else
    {
        s.controls.set_volume(s.value[1], commit);
    }
    s.applied[i] = s.value[i];
}

static float active_target(int id)
{
    if (id == BTN_DND) return s.controls.get_dnd() ? 1.0f : 0.0f;
    if (id == BTN_UPDATE_AUTO) return net_ota_get_auto() ? 1.0f : 0.0f;
    if (id == BTN_EYES_STYLE) return s.controls.get_eyes_style && s.controls.get_eyes_style() == 1 ? 1.0f : 0.0f;
    return 0.0f;
}

static void frame_timer_cb(lv_timer_t *timer)
{
    uint32_t now = lv_tick_get();
    float dt = LV_MIN((float)lv_tick_diff(now, s.last_tick) / 1000.0f, 0.05f);
    s.last_tick = now;
    bool busy = false;
    if (s.page == PAGE_WIFI && s.info.wifi_stage == 3 && !s.page_fading) {
        busy = true;
        if (lv_tick_elaps(s.spinner_tick) >= 80) {
            rect_t old = spinner_rect(s.spinner_angle);
            s.spinner_angle = (s.spinner_angle + 24) % 360;
            s.spinner_tick = now;
            rect_t next = spinner_rect(s.spinner_angle);
            mark_dirty((rect_t){LV_MIN(old.x0, next.x0), LV_MIN(old.y0, next.y0),
                                LV_MAX(old.x1, next.x1), LV_MAX(old.y1, next.y1)});
        }
    }
    if (s.page == PAGE_MAIN)
    {
        for (int i = 0; i < 2; i++)
        {
            if (s.value[i] != s.applied[i])
            {
                apply_slider(i, false);
            }
            float level = s.level[i];
            /* The fill glides after the finger between touch samples instead of jumping. */
            approach(&s.level[i], level_of(i, s.value[i]), dt, 0.05f);
            bool showing = s.drag == i || lv_tick_elaps(s.released_at[i]) < VALUE_SHOW_MS;
            float alpha = s.label_alpha[i];
            approach(&s.label_alpha[i], showing ? 1.0f : 0.0f, dt, showing ? 0.06f : 0.12f);
            if (s.level[i] != level || s.label_alpha[i] != alpha)
            {
                mark_dirty(slider_dirty_rect(i));
            }
            busy |= showing || s.level[i] != level_of(i, s.value[i]) || s.label_alpha[i] > 0.0f;
        }
    }
    for (int id = 0; id < BTN_COUNT; id++)
    {
        if (!button_on_page(id))
        {
            continue;
        }
        bool held = (s.pressed == id && !s.press_cancelled) || lv_tick_elaps(s.tap_until[id]) > 0x7FFFFFFF;
        float press = s.press[id], active = s.active[id];
        approach(&s.press[id], held ? 1.0f : 0.0f, dt, held ? 0.035f : 0.09f);
        approach(&s.active[id], active_target(id), dt, 0.08f);
        if (s.press[id] != press || s.active[id] != active)
        {
            render_button(id);
        }
        busy |= held || s.press[id] > 0.0f || s.active[id] != active_target(id);
    }
    flush_dirty();
    if (!busy && s.drag < 0 && s.pressed < 0)
    {
        lv_timer_pause(timer);
    }
}

static void wake(void)
{
    if (s.frame_timer && !s.frozen)
    {
        if (lv_timer_get_paused(s.frame_timer))
        {
            s.last_tick = lv_tick_get() - FRAME_MS;
            lv_timer_resume(s.frame_timer);
        }
        lv_timer_ready(s.frame_timer);
    }
}

static void info_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!page_has_live_info() || s.page_fading || s.frozen)
    {
        return;
    }
    info_t info;
    build_info(s.page, &info);
    if (memcmp(&info, &s.info, sizeof(info)) != 0)
    {
        s.info = info;
        if (s.page == PAGE_WIFI || s.page == PAGE_PORTAL) {
            s_cache.button_valid[BTN_BACK] = false;
            s_cache.button_valid[BTN_WIFI_SETUP] = false;
            wake();
        }
        if (s.page == PAGE_ABOUT) {
            int max_scroll = s.info.count * ROW_HEIGHT + ABOUT_FOOTER_H - (BODY_Y1 - BODY_Y0);
            s.info_scroll = clampi(s.info_scroll, 0, max_scroll);
        }
        render(s.page == PAGE_WIFI || s.page == PAGE_PORTAL ? (rect_t){0, 0, FACE_SIZE, FACE_SIZE} : body_rect());
    }
}

/* ---------------------------------------------------------------- pages */

static void page_fade_done(void *ctx)
{
    (void)ctx;
    free_frame(s.prev);
    s.prev = NULL;
    s.page_fading = false;
    if (s.page == PAGE_WIFI && s.info.wifi_stage == 3) wake();
}

static void show_page(page_t page)
{
    if (s.page_fading || page == s.page)
    {
        return;
    }
    size_t size = (size_t)FACE_SIZE * FACE_SIZE * sizeof(uint16_t);
    s.prev = s.direct ? alloc_frame(size) : NULL;
    if (s.prev)
    {
        memcpy(s.prev, s.panel, size);
    }
    s.page = page;
    s.confirm_setup = false;
    s.qr_pressed = false;
    s.info_scroll = 0;
    s.info_drag = false;
    s.pressed = -1;
    s.drag = -1;
    memset(s.press, 0, sizeof(s.press));
    memset(s.tap_until, 0, sizeof(s.tap_until));
    for (int id = 0; id < BTN_COUNT; id++)
    {
        s.active[id] = active_target(id);
    }
    s.dirty_count = 0;
    s_cache.button_valid[BTN_BACK] = false; /* geometry and surrounding page can change */
    s_cache.button_valid[BTN_WIFI_SETUP] = false;
    s_cache.button_valid[BTN_UPDATE_CHECK] = false;
    s_cache.button_valid[BTN_UPDATE_AUTO] = false;
    s_cache.button_valid[BTN_EYES_PREV] = false;
    s_cache.button_valid[BTN_EYES_NEXT] = false;
    s_cache.button_valid[BTN_EYES_STYLE] = false;
    build_info(page, &s.info);
    s.page_fading = s.prev != NULL;
    compose_page();
    if (!s.prev)
    {
        present((rect_t){0, 0, FACE_SIZE, FACE_SIZE});
    }
    else if (!ui_panel_fade(s.prev, s.panel, PAGE_FADE_MS, page_fade_done, NULL))
    {
        page_fade_done(NULL);
        present((rect_t){0, 0, FACE_SIZE, FACE_SIZE});
    }
    wake();
}

static void set_setup_confirmation(bool visible)
{
    s.confirm_setup = visible;
    s.pressed = -1;
    memset(s.press, 0, sizeof(s.press));
    memset(s.tap_until, 0, sizeof(s.tap_until));
    s_cache.button_valid[BTN_SETUP_NO] = false;
    s_cache.button_valid[BTN_SETUP_YES] = false;
    s_cache.button_valid[BTN_BACK] = false;
    s_cache.button_valid[BTN_WIFI_SETUP] = false;
    render((rect_t){0, 0, FACE_SIZE, FACE_SIZE});
    wake();
}

static void activate(int id)
{
    switch (id)
    {
    case BTN_WIFI:
        show_page(PAGE_WIFI);
        break;
    case BTN_WIFI_SETUP: {
        net_wifi_status_t status;
        net_wifi_get_status(&status);
        if (status.state == NET_WIFI_CONNECTED) set_setup_confirmation(true);
        else net_wifi_start_setup();
        break;
    }
    case BTN_SETUP_NO:
        set_setup_confirmation(false);
        break;
    case BTN_SETUP_YES:
        if (net_wifi_start_setup()) set_setup_confirmation(false);
        break;
    case BTN_PORTAL:
        show_page(PAGE_PORTAL);
        break;
    case BTN_ABOUT:
        show_page(PAGE_ABOUT);
        break;
    case BTN_UPDATE:
        show_page(PAGE_UPDATE);
        break;
    case BTN_EYES:
        show_page(PAGE_EYES);
        break;
    case BTN_EYES_STYLE:
        if (s.controls.get_eyes_style && s.controls.set_eyes_style)
        {
            s.controls.set_eyes_style(s.controls.get_eyes_style() == 1 ? 0 : 1);
            s_cache.button_valid[BTN_EYES_STYLE] = false;
            render((rect_t){0, 0, FACE_SIZE, FACE_SIZE});
            wake();
        }
        break;
    case BTN_EYES_PREV:
    case BTN_EYES_NEXT:
        if (s.controls.get_eyes_theme && s.controls.set_eyes_theme)
        {
            int count = eyes_themes_count();
            int next = (s.controls.get_eyes_theme() + (id == BTN_EYES_NEXT ? 1 : count - 1)) % count;
            s.controls.set_eyes_theme(next);
            /* The arrows' cached skins sit over the page background; they must not keep an old theme. */
            s_cache.button_valid[BTN_EYES_PREV] = false;
            s_cache.button_valid[BTN_EYES_NEXT] = false;
            render((rect_t){0, 0, FACE_SIZE, FACE_SIZE});
        }
        break;
    case BTN_UPDATE_CHECK: {
        net_wifi_status_t status;
        net_wifi_get_status(&status);
        if (status.state == NET_WIFI_CONNECTED) net_ota_start(); /* progress shows in the rows */
        break;
    }
    case BTN_UPDATE_AUTO:
        net_ota_set_auto(!net_ota_get_auto());
        wake();
        break;
    case BTN_BACK:
        show_page(PAGE_MAIN);
        break;
    case BTN_DND:
        s.controls.set_dnd(!s.controls.get_dnd());
        break;
    case BTN_SLEEP:
        s.controls.sleep();
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- input */

static lv_point_t touch_point(void)
{
    lv_point_t p = {0, 0};
    lv_indev_t *indev = lv_indev_active();
    if (indev)
    {
        lv_point_t raw;
        lv_indev_get_point(indev, &raw);
        int x, y;
        to_upright(raw.x, raw.y, &x, &y);
        p.x = x;
        p.y = y;
    }
    return p;
}

static int hit_slider(lv_point_t p)
{
    if (s.page != PAGE_MAIN)
    {
        return -1;
    }
    for (int i = 0; i < 2; i++)
    {
        if (abs(p.x - SLIDER_CX[i]) <= SLIDER_W / 2 + 6 && p.y >= SLIDER_TOP - 8 && p.y <= SLIDER_BOTTOM + 8)
        {
            return i;
        }
    }
    return -1;
}

static int hit_button(lv_point_t p, int slack)
{
    for (int id = 0; id < BTN_COUNT; id++)
    {
        const button_def_t *b = button_def(id);
        int r = b->size / 2 + slack;
        int dx = p.x - b->cx, dy = p.y - b->cy;
        if (button_on_page(id) && dx * dx + dy * dy <= r * r)
        {
            return id;
        }
    }
    return -1;
}

static void end_drag(void)
{
    if (s.drag < 0)
    {
        return;
    }
    int i = s.drag;
    s.drag = -1;
    s.released_at[i] = lv_tick_get();
    apply_slider(i, true);
    if (i == 1 && s.value[i] != s.drag_start_value)
    {
        audio_play_index(MMAP_SOUNDS_TOUCH_WAV);
    }
}

static void input_cb(lv_event_t *e)
{
    if (s.frozen || s.page_fading)
    {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p = touch_point();
    if (code == LV_EVENT_PRESSED && s.page == PAGE_WIFI && !s.confirm_setup &&
        s.info.wifi_stage > 0 && s.info.wifi_stage <= 2 && s.info.wifi_qr[0] && p.x >= 97 && p.x <= 369 && p.y >= 86 && p.y <= 358) {
        net_wifi_status_t status;
        net_wifi_get_status(&status);
        if (status.state == NET_WIFI_CONNECTED) {
            s.qr_pressed = true;
            s.press_start = p;
            return;
        }
    }
    if (s.qr_pressed) {
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            s.qr_pressed = false;
            int dx = p.x - s.press_start.x, dy = p.y - s.press_start.y;
            if (code == LV_EVENT_RELEASED && dx * dx + dy * dy <= 16 * 16)
                set_setup_confirmation(true);
        }
        return;
    }
    if (code == LV_EVENT_PRESSED && s.page == PAGE_ABOUT &&
        p.x >= ROW_LEFT && p.x <= ROW_RIGHT && p.y >= BODY_Y0 && p.y < BODY_Y1) {
        s.info_drag = true;
        s.info_touch_y = p.y;
        return;
    }
    if (s.info_drag) {
        if (code == LV_EVENT_PRESSING) {
            int max_scroll = s.info.count * ROW_HEIGHT + ABOUT_FOOTER_H - (BODY_Y1 - BODY_Y0);
            s.info_scroll = clampi(s.info_scroll + s.info_touch_y - p.y, 0, max_scroll);
            s.info_touch_y = p.y;
            render(body_rect());
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            s.info_drag = false;
        }
        return;
    }
    if (code == LV_EVENT_PRESSED)
    {
        int slider = hit_slider(p);
        if (slider >= 0)
        {
            s.drag = slider;
            s.drag_start_y = p.y;
            s.drag_start_value = s.value[slider];
        }
        else
        {
            s.pressed = hit_button(p, 8);
            s.press_cancelled = false;
            s.press_start = p;
        }
        wake();
    }
    else if (code == LV_EVENT_PRESSING)
    {
        if (s.drag >= 0)
        {
            int i = s.drag;
            int range = 100 - slider_min(i);
            int value = s.drag_start_value +
                        (int)lroundf((float)(s.drag_start_y - p.y) * (float)range / (float)(FILL_MAX - FILL_MIN));
            value = clampi(value, slider_min(i), 100);
            if (value != s.value[i])
            {
                s.value[i] = value;
                wake();
            }
        }
        else if (s.pressed >= 0 && !s.press_cancelled)
        {
            int dx = p.x - s.press_start.x, dy = p.y - s.press_start.y;
            if (dx * dx + dy * dy > 16 * 16 || hit_button(p, 14) != s.pressed)
            {
                s.press_cancelled = true;
                wake();
            }
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
        end_drag();
        int id = s.pressed;
        bool cancelled = s.press_cancelled;
        s.pressed = -1;
        lv_indev_t *indev = lv_indev_active();
        bool swiped = indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE;
        if (id >= 0 && !cancelled && !swiped && code == LV_EVENT_RELEASED)
        {
            /* Hold the pressed look briefly so even a quick tap visibly lands. */
            s.tap_until[id] = lv_tick_get() + 90;
            wake();
            activate(id);
        }
        else
        {
            wake();
        }
    }
}

/* ---------------------------------------------------------------- lifecycle */

static void release_resources(void)
{
    if (s.frame_timer)
    {
        lv_timer_delete(s.frame_timer);
        s.frame_timer = NULL;
    }
    if (s.info_timer)
    {
        lv_timer_delete(s.info_timer);
        s.info_timer = NULL;
    }
    ui_panel_strips_free(&s.strips);
    if (s.direct)
    {
        eyes_panel_end();
        s.direct = false;
    }
    free_frame(s.work);
    free_frame(s.panel);
    free_frame(s.prev);
    s.work = s.panel = s.prev = NULL;
    s.scr = s.canvas = s.draw_canvas = NULL;
}

static void delete_cb(lv_event_t *e)
{
    (void)e;
    end_drag();
    release_resources();
}

lv_obj_t *ui_screen_settings_create(const ui_settings_controls_t *controls)
{
    release_resources();
    memset(&s, 0, sizeof(s));
    s.controls = *controls;
    s.drag = -1;
    s.pressed = -1;
    s.page = PAGE_MAIN;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_clickable(scr, true);
    s.scr = scr;
    lv_obj_add_event_cb(scr, delete_cb, LV_EVENT_DELETE, NULL);

    size_t size = (size_t)FACE_SIZE * FACE_SIZE * sizeof(uint16_t);
    s.work = alloc_frame(size);
    s.panel = alloc_frame(size);
    if (!s.work || !s.panel || !build_caches())
    {
        free_frame(s.work);
        free_frame(s.panel);
        s.work = s.panel = NULL;
        return scr;
    }

    for (int i = 0; i < 2; i++)
    {
        s.value[i] = clampi(i == 0 ? s.controls.get_brightness() : s.controls.get_volume(), slider_min(i), 100);
        s.applied[i] = s.value[i];
        s.level[i] = level_of(i, s.value[i]);
        s.released_at[i] = lv_tick_get() - VALUE_SHOW_MS;
    }
    s.active[BTN_DND] = s.controls.get_dnd() ? 1.0f : 0.0f;

    s.draw_canvas = lv_canvas_create(scr);
    lv_obj_set_hidden(s.draw_canvas, true);
    lv_canvas_set_buffer(s.draw_canvas, s.work, FACE_SIZE, FACE_SIZE, LV_COLOR_FORMAT_RGB565);

    /* Shown by LVGL only when direct drawing is unavailable. */
    s.canvas = lv_canvas_create(scr);
    lv_obj_remove_style_all(s.canvas);
    lv_canvas_set_buffer(s.canvas, s.panel, FACE_SIZE, FACE_SIZE, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(s.canvas);
    lv_obj_set_clickable(s.canvas, false);

    /* Own the panel for the shade's whole life: opening cross-fades straight into this frame and
     * every later change is sent directly, so LVGL never redraws (or flickers) the full screen. */
    s.direct = eyes_panel_begin();
    if (s.direct && !ui_panel_strips_init(&s.strips, STRIP_PIXELS))
    {
        eyes_panel_end();
        s.direct = false;
    }
    if (s.direct)
    {
        lv_obj_set_hidden(s.canvas, true);
    }

    /* Compose only: the caller decides how this first frame reaches the panel. */
    compose_page();

    lv_obj_add_event_cb(scr, input_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, input_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, input_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, input_cb, LV_EVENT_PRESS_LOST, NULL);
    s.frame_timer = lv_timer_create(frame_timer_cb, FRAME_MS, NULL);
    lv_timer_pause(s.frame_timer);
    s.info_timer = lv_timer_create(info_timer_cb, INFO_MS, NULL);
    return scr;
}

void ui_screen_settings_prewarm(void)
{
    if (s.scr || s_cache.main)
    {
        return;
    }
    memset(&s, 0, sizeof(s));
    s.drag = -1;
    s.pressed = -1;
    s.work = alloc_frame((size_t)FACE_SIZE * FACE_SIZE * sizeof(uint16_t));
    s.draw_canvas = s.work ? lv_canvas_create(lv_layer_sys()) : NULL;
    if (s.draw_canvas)
    {
        lv_obj_set_hidden(s.draw_canvas, true);
        lv_canvas_set_buffer(s.draw_canvas, s.work, FACE_SIZE, FACE_SIZE, LV_COLOR_FORMAT_RGB565);
        if (build_caches())
        {
            s.page = PAGE_MAIN;
            draw_scene((rect_t){0, 0, FACE_SIZE, FACE_SIZE});
            s_cache.main = alloc_frame((size_t)FACE_SIZE * FACE_SIZE * sizeof(uint16_t));
            if (s_cache.main)
            {
                memcpy(s_cache.main, s.work, (size_t)FACE_SIZE * FACE_SIZE * sizeof(uint16_t));
            }
            for (int id = 0; id < BTN_COUNT; id++)
            {
                if (button_on_page(id))
                {
                    ensure_button_cache(id);
                }
            }
        }
        lv_obj_delete(s.draw_canvas);
    }
    free_frame(s.work);
    memset(&s, 0, sizeof(s));
}

const uint16_t *ui_screen_settings_panel_pixels(void)
{
    return s.panel;
}

bool ui_screen_settings_touch_captured(void)
{
    return s.drag >= 0 || s.info_drag || s.page_fading;
}

bool ui_screen_settings_back(void)
{
    if (s.confirm_setup) { set_setup_confirmation(false); return true; }
    if (s.page == PAGE_MAIN || s.frozen || s.page_fading)
    {
        return false;
    }
    show_page(PAGE_MAIN);
    return true;
}

void ui_screen_settings_freeze(void)
{
    end_drag();
    s.info_drag = false;
    s.pressed = -1;
    s.frozen = true;
    if (s.frame_timer)
    {
        lv_timer_pause(s.frame_timer);
    }
}

#ifdef UI_SETTINGS_TEST_HOOKS
bool ui_screen_settings_test_confirm_setup(void) { return s.confirm_setup; }
int ui_screen_settings_test_wifi_stage(void) { return s.info.wifi_stage; }
const char *ui_screen_settings_test_wifi_qr(void) { return s.info.wifi_qr; }

int ui_screen_settings_test_scroll(void)
{
    return s.info_scroll;
}

int ui_screen_settings_test_page(void)
{
    return s.page;
}

void ui_screen_settings_test_info(char *out, size_t size)
{
    size_t used = 0;
    out[0] = '\0';
    for (int k = 0; k < s.info.count && used < size; k++)
    {
        used += (size_t)snprintf(out + used, size - used, "%s=%s;", s.info.rows[k].label, s.info.rows[k].value);
    }
    if (used < size)
    {
        snprintf(out + used, size - used, "%s", s.info.note);
    }
}
#endif
