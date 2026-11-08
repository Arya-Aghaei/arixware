#include "ui_draw.h"
#include "ui_orient.h"
#include "eyes_panel.h"
#include "esp_heap_caps.h"
#include <math.h>

#define PULSE_PERIOD_MS 1400
#define PULSE_TIMER_MS 16
#define STRIP_ROWS 22

/* Same mapping as an LVGL transform_rotation of UI_ROTATION_DEG (clockwise, about the center),
 * walked in tiles so both the PSRAM reads and writes stay cache-friendly. */
static void rotate_into(uint16_t *dst, const uint16_t *src)
{
    const int32_t n = UI_FACE_SIZE;
    const int32_t tile = 16;
    for (int32_t ty = 0; ty < n; ty += tile) {
        for (int32_t tx = 0; tx < n; tx += tile) {
            int32_t ye = LV_MIN(ty + tile, n);
            int32_t xe = LV_MIN(tx + tile, n);
            for (int32_t ly = ty; ly < ye; ly++) {
                for (int32_t lx = tx; lx < xe; lx++) {
                    int32_t px;
                    int32_t py;
                    switch (UI_ROTATION_DEG) {
                    case 90: px = n - 1 - ly; py = lx; break;
                    case 180: px = n - 1 - lx; py = n - 1 - ly; break;
                    case 270: px = ly; py = n - 1 - lx; break;
                    default: px = lx; py = ly; break;
                    }
                    dst[py * n + px] = src[ly * n + lx];
                }
            }
        }
    }
}

static uint16_t *alloc_face_buffer(void)
{
    size_t size = UI_FACE_SIZE * UI_FACE_SIZE * sizeof(uint16_t);
    /* PSRAM: internal RAM is a scarce pool shared with the SPI driver's DMA descriptors. */
    uint16_t *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return buf ? buf : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

static void pulse_cb(lv_timer_t *timer)
{
    ui_face_t *face = lv_timer_get_user_data(timer);
    if (!face->direct || !face->alt_buf || ui_panel_fade_active()) {
        return;
    }
    float t = (float)(lv_tick_get() % PULSE_PERIOD_MS) / PULSE_PERIOD_MS;
    float level = 0.5f - 0.5f * cosf(t * 6.2831853f);
    ui_panel_blend(&face->strips, face->buf, face->alt_buf, (uint32_t)lroundf(level * 32.0f));
}

static void update_pulse_timer(ui_face_t *face)
{
    bool run = face->pulse_wanted && face->direct && face->alt_buf;
    if (run && !face->pulse) {
        face->pulse = lv_timer_create(pulse_cb, PULSE_TIMER_MS, face);
    } else if (!run && face->pulse) {
        lv_timer_delete(face->pulse);
        face->pulse = NULL;
    }
}

static void present(ui_face_t *face)
{
    if (face->direct) {
        if (!ui_panel_fade_active()) {
            ui_panel_send(&face->strips, face->buf, 0, 0, UI_FACE_SIZE, UI_FACE_SIZE);
        }
    } else {
        lv_obj_invalidate(face->canvas);
    }
}

/* The transition into this screen has already put these exact pixels on the panel, so taking the
 * panel now (before LVGL's first render) changes nothing visible. */
static void loaded_cb(lv_event_t *e)
{
    ui_face_t *face = lv_event_get_user_data(e);
    if (face->direct || !face->buf || !eyes_panel_begin()) {
        return;
    }
    if (!ui_panel_strips_init(&face->strips, UI_FACE_SIZE * STRIP_ROWS)) {
        eyes_panel_end();
        return;
    }
    face->direct = true;
    update_pulse_timer(face);
}

lv_obj_t *ui_face_create(ui_face_t *face)
{
    *face = (ui_face_t){ 0 };
    face->buf = alloc_face_buffer();
    face->draw_buf = alloc_face_buffer();
    if (!face->buf || !face->draw_buf) {
        heap_caps_free(face->buf);
        heap_caps_free(face->draw_buf);
        face->buf = face->draw_buf = NULL;
        return NULL;
    }

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    face->draw_canvas = lv_canvas_create(scr);
    lv_obj_set_hidden(face->draw_canvas, true);
    lv_canvas_set_buffer(face->draw_canvas, face->draw_buf, UI_FACE_SIZE, UI_FACE_SIZE,
                         LV_COLOR_FORMAT_RGB565);

    face->canvas = lv_canvas_create(scr);
    lv_obj_remove_style_all(face->canvas);
    lv_canvas_set_buffer(face->canvas, face->buf, UI_FACE_SIZE, UI_FACE_SIZE, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(face->canvas);
    lv_obj_add_event_cb(scr, loaded_cb, LV_EVENT_SCREEN_LOADED, face);
    return scr;
}

void ui_face_destroy(ui_face_t *face)
{
    face->pulse_wanted = false;
    update_pulse_timer(face);
    if (face->direct) {
        face->direct = false;
        ui_panel_strips_free(&face->strips);
        eyes_panel_end();
    }
    heap_caps_free(face->buf);
    heap_caps_free(face->draw_buf);
    heap_caps_free(face->alt_buf);
    face->buf = face->draw_buf = face->alt_buf = NULL;
    face->canvas = face->draw_canvas = NULL;
}

void ui_face_set_pulse(ui_face_t *face, bool on)
{
    face->pulse_wanted = on;
    update_pulse_timer(face);
}

void ui_face_begin_plain(ui_face_t *face, lv_layer_t *layer)
{
    lv_canvas_fill_bg(face->draw_canvas, lv_color_black(), LV_OPA_COVER);
    lv_canvas_init_layer(face->draw_canvas, layer);
}

void ui_face_begin(ui_face_t *face, lv_layer_t *layer, image_id_t background)
{
    ui_face_begin_plain(face, layer);
    const lv_image_dsc_t *bg = images_get(background);
    if (bg) {
        lv_draw_image_dsc_t img;
        lv_draw_image_dsc_init(&img);
        img.src = bg;
        lv_area_t area = { 0, 0, UI_FACE_SIZE - 1, UI_FACE_SIZE - 1 };
        lv_draw_image(layer, &img, &area);
    }
}

void ui_face_end(ui_face_t *face, lv_layer_t *layer)
{
    lv_canvas_finish_layer(face->draw_canvas, layer);
    rotate_into(face->buf, face->draw_buf);
    present(face);
}

bool ui_face_end_peak(ui_face_t *face, lv_layer_t *layer)
{
    lv_canvas_finish_layer(face->draw_canvas, layer);
    if (!face->alt_buf) {
        face->alt_buf = alloc_face_buffer();
    }
    if (!face->alt_buf) {
        return false;
    }
    rotate_into(face->alt_buf, face->draw_buf);
    update_pulse_timer(face);
    return true;
}

void ui_face_pointer(int *x, int *y)
{
    lv_point_t p = { 0, 0 };
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &p);
    }
    switch (UI_ROTATION_DEG) {
    case 90: *x = p.y; *y = UI_FACE_SIZE - 1 - p.x; break;
    case 180: *x = UI_FACE_SIZE - 1 - p.x; *y = UI_FACE_SIZE - 1 - p.y; break;
    case 270: *x = UI_FACE_SIZE - 1 - p.y; *y = p.x; break;
    default: *x = p.x; *y = p.y; break;
    }
}

void ui_draw_text(lv_layer_t *layer, const char *text, const lv_font_t *font,
                  int cx, int cy, uint32_t color, lv_opa_t opa)
{
    int h = lv_font_get_line_height(font);
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.text_local = 1;
    dsc.font = font;
    dsc.align = LV_TEXT_ALIGN_CENTER;
    dsc.color = lv_color_hex(color);
    dsc.opa = opa;
    lv_area_t area = { cx - 220, cy - h / 2, cx + 220, cy + h / 2 };
    lv_draw_label(layer, &dsc, &area);
}

void ui_draw_glow_text(lv_layer_t *layer, const char *text, const lv_font_t *font,
                       int cx, int cy, uint32_t color)
{
    static const int8_t offs[][2] = { { -3, 0 }, { 3, 0 }, { 0, -3 }, { 0, 3 },
                                      { -3, -3 }, { 3, 3 }, { -3, 3 }, { 3, -3 } };
    for (size_t i = 0; i < sizeof(offs) / sizeof(offs[0]); i++) {
        ui_draw_text(layer, text, font, cx + offs[i][0], cy + offs[i][1], UI_COLOR_PRIMARY, LV_OPA_50);
    }
    ui_draw_text(layer, text, font, cx, cy, color, LV_OPA_COVER);
}

void ui_draw_disc(lv_layer_t *layer, int cx, int cy, int r, uint32_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = opa;
    dsc.radius = LV_RADIUS_CIRCLE;
    lv_area_t area = { cx - r, cy - r, cx + r, cy + r };
    lv_draw_rect(layer, &dsc, &area);
}

void ui_draw_ring(lv_layer_t *layer, int cx, int cy, int r, int width, uint32_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_color = lv_color_hex(color);
    dsc.border_opa = opa;
    dsc.border_width = width;
    dsc.radius = LV_RADIUS_CIRCLE;
    lv_area_t area = { cx - r, cy - r, cx + r, cy + r };
    lv_draw_rect(layer, &dsc, &area);
}

void ui_draw_halo(lv_layer_t *layer, int cx, int cy, int inner, int outer, uint32_t color, lv_opa_t peak)
{
    /* Very low-opacity blends tint green on an RGB565 canvas (the red channel rounds away first),
     * so stack a few moderately translucent discs of a pre-dimmed colour instead. Four layers of
     * 30% reach ~76% of the dimmed colour at the centre. */
    const int steps = 4;
    const lv_opa_t layer_opa = LV_OPA_30;
    uint32_t dim = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        uint32_t channel = (color >> shift) & 0xFF;
        channel = channel * peak / 255 * 100 / 76;
        dim = dim << 8 | (channel > 255 ? 255 : channel);
    }
    for (int i = 0; i < steps; i++) {
        int r = outer - (outer - inner) * i / steps;
        ui_draw_disc(layer, cx, cy, r, dim, layer_opa);
    }
}

void ui_draw_arc(lv_layer_t *layer, int cx, int cy, int r, int width, int start_deg, int end_deg,
                 uint32_t color, lv_opa_t opa)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = lv_color_hex(color);
    dsc.opa = opa;
    dsc.width = width;
    dsc.center.x = cx;
    dsc.center.y = cy;
    dsc.radius = r;
    dsc.start_angle = start_deg;
    dsc.end_angle = end_deg;
    dsc.rounded = 1;
    lv_draw_arc(layer, &dsc);
}

void ui_draw_pill(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t fill, lv_opa_t fill_opa,
                  uint32_t border, int border_width)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(fill);
    dsc.bg_opa = fill_opa;
    dsc.border_color = lv_color_hex(border);
    dsc.border_width = border_width;
    dsc.border_opa = LV_OPA_COVER;
    dsc.radius = LV_RADIUS_CIRCLE;
    lv_area_t area = { x1, y1, x2, y2 };
    lv_draw_rect(layer, &dsc, &area);
}

void ui_draw_triangle(lv_layer_t *layer, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color)
{
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.p[0].x = x1; dsc.p[0].y = y1;
    dsc.p[1].x = x2; dsc.p[1].y = y2;
    dsc.p[2].x = x3; dsc.p[2].y = y3;
    dsc.color = lv_color_hex(color);
    dsc.opa = LV_OPA_COVER;
    lv_draw_triangle(layer, &dsc);
}

static uint32_t orb_color(uint32_t rgb, bool dim)
{
    return dim ? lv_color_to_u32(lv_color_mix(lv_color_hex(rgb), lv_color_hex(0x7A5A3A), 110)) & 0xFFFFFF : rgb;
}

void ui_draw_orb(lv_layer_t *layer, int cx, int cy, int r, const eyes_theme_t *theme, bool peak, bool dim)
{
    ui_draw_halo(layer, cx, cy, r, r + (peak ? 26 : 18), orb_color(theme->glow, dim), dim ? 40 : (peak ? 150 : 100));
    ui_draw_disc(layer, cx, cy, r + 2, orb_color(theme->rim, dim), LV_OPA_COVER);
    ui_draw_ring(layer, cx, cy, r + 3, 2, orb_color(theme->glow, dim), LV_OPA_COVER);

    int in = r - 7;
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.radius = LV_RADIUS_CIRCLE;
    rect.bg_opa = LV_OPA_COVER;
    rect.bg_grad.dir = LV_GRAD_DIR_VER;
    rect.bg_grad.stops_count = 2;
    rect.bg_grad.stops[0].color = lv_color_hex(orb_color(theme->fill_top, dim));
    rect.bg_grad.stops[0].opa = LV_OPA_COVER;
    rect.bg_grad.stops[0].frac = 0;
    rect.bg_grad.stops[1].color = lv_color_hex(orb_color(theme->fill_bottom, dim));
    rect.bg_grad.stops[1].opa = LV_OPA_COVER;
    rect.bg_grad.stops[1].frac = 255;
    lv_area_t area = { cx - in, cy - in, cx + in, cy + in };
    lv_draw_rect(layer, &rect, &area);

    /* Halftone: dots grow towards the bottom edge, clipped to the orb. */
    for (int row = 0; row < 5; row++) {
        int y = cy + in * 2 / 10 + row * (r / 6);
        int dot = 1 + row * r / 60 + (row > 2 ? 1 : 0);
        for (int x = cx - in; x <= cx + in; x += r / 5 + 1) {
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= (in - 3 - dot) * (in - 3 - dot)) {
                ui_draw_disc(layer, x + (row & 1) * (r / 10), y, dot, orb_color(theme->fill_mid, dim), LV_OPA_70);
            }
        }
    }
    ui_draw_arc(layer, cx, cy, in * 3 / 4, LV_MAX(3, r / 7), 215, 250, 0xFFFFFF, LV_OPA_70);
    ui_draw_disc(layer, cx - in * 3 / 5, cy - in * 2 / 5 + 2, LV_MAX(2, r / 12), 0xFFFFFF, LV_OPA_70);
}

static void rounded(lv_layer_t *layer, int x0, int y0, int x1, int y1, int radius, uint32_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    d.bg_opa = opa;
    d.radius = radius;
    lv_area_t a = { x0, y0, x1, y1 };
    lv_draw_rect(layer, &d, &a);
}

void ui_draw_eye(lv_layer_t *layer, int cx, int cy, int r, const eyes_theme_t *theme, int style, bool peak, bool dim)
{
    if (style != 1) {
        ui_draw_orb(layer, cx, cy, r, theme, peak, dim);
        return;
    }
    /* Glass: a tall rounded body with a dark gap and a thin outline, a pupil under a lid. */
    int hw = r * 62 / 100, hh = r * 88 / 100, rad = hw * 60 / 100;
    for (int i = 4; i >= 1; i--) {
        rounded(layer, cx - hw - 7 - i * 3, cy - hh - 7 - i * 3, cx + hw + 7 + i * 3, cy + hh + 7 + i * 3, rad + 7 + i * 3,
                orb_color(theme->glow, dim), (lv_opa_t)((dim ? 10 : 22) * (5 - i) / 2));
    }
    rounded(layer, cx - hw - 8, cy - hh - 8, cx + hw + 8, cy + hh + 8, rad + 8, orb_color(theme->ring, dim), LV_OPA_COVER);
    rounded(layer, cx - hw - 6, cy - hh - 6, cx + hw + 6, cy + hh + 6, rad + 6, 0x000000, LV_OPA_COVER);
    lv_draw_rect_dsc_t body;
    lv_draw_rect_dsc_init(&body);
    body.radius = rad;
    body.bg_opa = LV_OPA_COVER;
    body.bg_grad.dir = LV_GRAD_DIR_VER;
    body.bg_grad.stops_count = 2;
    body.bg_grad.stops[0].color = lv_color_hex(orb_color(theme->fill_top, dim));
    body.bg_grad.stops[0].opa = LV_OPA_COVER;
    body.bg_grad.stops[0].frac = 0;
    body.bg_grad.stops[1].color = lv_color_hex(orb_color(theme->fill_bottom, dim));
    body.bg_grad.stops[1].opa = LV_OPA_COVER;
    body.bg_grad.stops[1].frac = 255;
    lv_area_t area = { cx - hw, cy - hh, cx + hw, cy + hh };
    lv_draw_rect(layer, &body, &area);
    /* a pupil and its catch-light */
    ui_draw_disc(layer, cx + hw / 5, cy + hh / 5, hw * 38 / 100, 0x05101A, LV_OPA_COVER);
    ui_draw_disc(layer, cx + hw / 5 - hw * 13 / 100, cy + hh / 5 - hw * 14 / 100, hw * 9 / 100 + 1, 0xFFFFFF, LV_OPA_90);
    /* the upper lid: black, with a bright edge */
    rounded(layer, cx - hw - 12, cy - hh - 12, cx + hw + 12, cy - hh * 3 / 10, 0, 0x000000, LV_OPA_COVER);
    rounded(layer, cx - hw - 12, cy - hh * 3 / 10, cx + hw + 12, cy - hh * 3 / 10 + 2, 0, orb_color(theme->ring, dim), LV_OPA_COVER);
}
