#include "eyes_raster.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* Glossy amber "orb" eyes. Shapes are evaluated as signed distances,
 * so every edge is anti-aliased and sizes are continuous,
 * and all of them share the orb material: gradient body, dark rim, bright outer ring, soft glow.
 * Round eyes add halftone dots and a gloss streak; the dizzy eye adds a spinning spiral. */
#define COLOR_EDGE      0x8B4A1F /* accent ticks (dark amber/brown) */

#define ORB_FILL_TOP    0xFFF9E3 /* cream */
#define ORB_FILL_MID    0xFFD460 /* warm yellow */
#define ORB_FILL_BOTTOM 0xFF8814 /* saturated orange */
#define ORB_FILL_MID_T  0.45f
#define ORB_RIM         0x4A1E06 /* dark brown band */
#define ORB_RING        0xFFC25A /* thin bright outer ring */
#define ORB_GLOW        0xFF7A0A /* soft outer bloom */
#define ORB_DOT         0xFFF3CC /* halftone dots */
#define ORB_DOT_OPA     0.5f
#define ORB_RING_W      2.5f  /* bright ring, from the outer edge inward */
#define ORB_RIM_W       5.5f  /* dark band inside the ring */
#define ORB_GLOW_R      9.0f  /* bloom reach outside the edge */
#define ORB_SHADOW_W    4.0f  /* inner shadow on the fill next to the rim */
#define DOT_ROWS_MAX    24

typedef struct {
    float r, g, b;
} rgb_t;

typedef struct {
    float hw, hh;   /* stadium half extents */
    float cx, cy;
} stadium_t;

typedef struct {
    float ax, ay, abx, aby, inv_len2;
} seg_t;

typedef struct {
    eyes_shape_t shape;
    stadium_t body;
    float grad_y0, grad_inv;
    int32_t grad_base; /* logical y of s_grad[0] */
    /* Highlight: an arc stroke in body-normalized space (u = x, v = (y - cy) * hw / hh). */
    float hl_vs;                     /* logical -> normalized vertical scale */
    float hl_px;                     /* normalized -> pixel distance scale */
    float hl_rm, hl_hw, hl_cos_half; /* arc mid radius, half width, sector half-angle */
    float hl_mx, hl_my;              /* sector mid direction */
    float hl_e1x, hl_e1y, hl_e2x, hl_e2y;
    float hl_alpha;
    float hl_r2_min, hl_r2_max; /* squared radii bounding the stroke incl. caps and feather */
    float glint_x0, glint_x1, glint_y0, glint_y1; /* highlight bounds (logical) */
    /* Halftone dots: rows of DOT spacing anchored above the bottom rim, growing downward. */
    float dot_sp, dot_inv_sp, dot_yb, dot_y0;
    float dot_r[DOT_ROWS_MAX];
    float dot_lim2[DOT_ROWS_MAX]; /* (r + 0.5)^2: beyond this a dot contributes nothing */
    float dot_in2[DOT_ROWS_MAX];  /* (r - 0.5)^2: within this a dot is fully opaque */
    /* Edge band without sqrt: stadium reduced to a circle of radius cap_r; squared distance
     * q2 in [q2_min, ...) maps through s_q2_lut (step Q2_STEP) to a s_cap_lut index. */
    float cap_r, q2_min, q2_inv_step;
    int32_t q2_n;
    /* Spiral (dizzy): two arms over the orb gradient, phase-shifted to spin. */
    float spin_dir, spin_phase, spiral_pitch, spiral_inv_pitch, spiral_amt;
    int32_t dot_rows;
    seg_t chev_a, chev_b;
    float half_main; /* chevron stroke half width */
    stadium_t bite;
    bool ticks_on;
    float ticks;
    seg_t tick; /* right tick; left is mirrored */
    float tick_x0, tick_x1, tick_y0, tick_y1; /* bounds on |x|, y */
    int32_t x0, y0, x1, y1;                   /* logical bounding box, inclusive */
    bool mirror_x;                            /* shape is symmetric under x -> -x ... */
    float asym_x0, asym_x1;                   /* ... except for x in this range (glint) */
    float lid_slope;
} geom_t;

static rgb_t s_edge;
static rgb_t s_orb_top, s_orb_mid, s_orb_bottom, s_orb_rim, s_orb_ring, s_orb_glow, s_orb_dot;

/* Capsule shading depends only on the edge distance d and the row's gradient color:
 * color = A(d) + fill(y) * k(d). A/k are sampled once over the band where anything but pure
 * fill or pure black happens; fill(y) is tabulated per pose. */
#define CAP_LUT_DMIN  (-(ORB_RING_W + ORB_RIM_W + ORB_SHADOW_W))
#define CAP_LUT_DMAX  ORB_GLOW_R
#define CAP_LUT_SCALE 16.0f
#define CAP_LUT_N     340 /* >= (DMAX - DMIN) * SCALE + 2 = 338 */
typedef struct {
    float ar, ag, ab, k;
} cap_lut_t;
static cap_lut_t s_cap_lut[CAP_LUT_N];

#define GRAD_MAX 320 /* > tallest capsule bbox (~220 px incl. glow, overshoot and ticks) */
static rgb_t s_grad[GRAD_MAX];
static uint16_t s_grad_packed[GRAD_MAX][4]; /* per row, dithered for |x| & 3 */
static uint16_t s_grad_dot_packed[GRAD_MAX][4]; /* same, under a fully covering halftone dot */
#define Q2_MAX  1400 /* entries; the step adapts so every pose fits (see build_q2_lut) */
static uint16_t s_q2_lut[Q2_MAX];

#define SPIRAL_ARMS  0.0f /* 0 = concentric rings, as the reference chart draws them */
#define SPIRAL_HALF  0.22f /* stripe half width, in arm periods */
#define SPIRAL_OPA   0.85f
#define SPIRAL_COLOR 0x6A2C08 /* dark amber stripes */
static rgb_t s_spiral;
static const rgb_t s_white = { 255.0f, 255.0f, 255.0f };
static const rgb_t s_black = { 0.0f, 0.0f, 0.0f };

static rgb_t hex(uint32_t c)
{
    rgb_t o = { (float)((c >> 16) & 0xFF), (float)((c >> 8) & 0xFF), (float)(c & 0xFF) };
    return o;
}

static inline float clampf(float lo, float v, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Pixel coverage from a signed distance (negative = inside), 1 px anti-aliasing ramp. */
static inline float cov(float d)
{
    return clampf(0.0f, 0.5f - d, 1.0f);
}

static inline void over(rgb_t *c, rgb_t s, float a)
{
    if (a > 0.0f) {
        c->r += (s.r - c->r) * a;
        c->g += (s.g - c->g) * a;
        c->b += (s.b - c->b) * a;
    }
}

static inline uint16_t pack(rgb_t c)
{
    int r = (int)(c.r + 0.5f);
    int g = (int)(c.g + 0.5f);
    int b = (int)(c.b + 0.5f);
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    /* The panel takes RGB565 big-endian. */
    return (uint16_t)((v >> 8) | (v << 8));
}

/* 4x4 ordered dither for RGB565: smooth gradients otherwise band visibly at 5-6 bits per channel.
 * Indexed by |x| so the pattern stays left/right symmetric (row mirroring relies on it). */
static const float s_bayer[4][4] = {
    { -0.469f, 0.031f, -0.344f, 0.156f },
    { 0.281f, -0.219f, 0.406f, -0.094f },
    { -0.281f, 0.219f, -0.406f, 0.094f },
    { 0.469f, -0.031f, 0.344f, -0.156f },
};

static inline uint16_t pack_dither(rgb_t c, int32_t ax, int32_t y)
{
    float o = s_bayer[y & 3][ax & 3];
    c.r = clampf(0.0f, c.r + o * 8.0f, 255.0f);
    c.g = clampf(0.0f, c.g + o * 4.0f, 255.0f);
    c.b = clampf(0.0f, c.b + o * 8.0f, 255.0f);
    return pack(c);
}

static inline int32_t iabs_x(float x)
{
    return (int32_t)fabsf(x);
}

/* sqrtf is a libm call on this Xtensa core; rsqrt bit trick + 2 Newton steps is ~1e-6
 * relative error (<0.001 px here) at a fraction of the cost. */
static inline float fast_sqrtf(float x)
{
    if (x <= 0.0f) {
        return 0.0f;
    }
    union {
        float f;
        uint32_t i;
    } u = { x };
    u.i = 0x5f375a86u - (u.i >> 1);
    float y = u.f;
    float hx = 0.5f * x;
    y = y * (1.5f - hx * y * y);
    y = y * (1.5f - hx * y * y);
    return x * y;
}

/* One Newton step (~0.2 % error, < 0.2 px here): only used where the result feeds a smooth
 * ramp (edge shading), never for geometry decisions. */
static inline float fast_sqrtf_lo(float x)
{
    if (x <= 0.0f) {
        return 0.0f;
    }
    union {
        float f;
        uint32_t i;
    } u = { x };
    u.i = 0x5f375a86u - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

static inline float sd_stadium_lo(float x, float y, float hw, float hh)
{
    float ax = fabsf(x);
    float ay = fabsf(y);
    if (hw >= hh) {
        float dx = ax - (hw - hh);
        dx = dx > 0.0f ? dx : 0.0f;
        return fast_sqrtf_lo(dx * dx + ay * ay) - hh;
    }
    float dy = ay - (hh - hw);
    dy = dy > 0.0f ? dy : 0.0f;
    return fast_sqrtf_lo(ax * ax + dy * dy) - hw;
}

/* Rounded rect whose corner radius is min(hw, hh), as LVGL's radius = min(w, h) / 2. */
static inline float sd_stadium(float x, float y, float hw, float hh)
{
    float ax = fabsf(x);
    float ay = fabsf(y);
    if (hw >= hh) {
        float dx = ax - (hw - hh);
        dx = dx > 0.0f ? dx : 0.0f;
        return fast_sqrtf(dx * dx + ay * ay) - hh;
    }
    float dy = ay - (hh - hw);
    dy = dy > 0.0f ? dy : 0.0f;
    return fast_sqrtf(ax * ax + dy * dy) - hw;
}

static seg_t make_seg(float ax, float ay, float bx, float by)
{
    seg_t sg = { ax, ay, bx - ax, by - ay, 0.0f };
    float len2 = sg.abx * sg.abx + sg.aby * sg.aby;
    sg.inv_len2 = len2 > 0.0f ? 1.0f / len2 : 0.0f;
    return sg;
}

static inline float seg_dist2(const seg_t *sg, float px, float py)
{
    float apx = px - sg->ax;
    float apy = py - sg->ay;
    float t = clampf(0.0f, (apx * sg->abx + apy * sg->aby) * sg->inv_len2, 1.0f);
    float dx = apx - sg->abx * t;
    float dy = apy - sg->aby * t;
    return dx * dx + dy * dy;
}

static void bbox_add(geom_t *g, float x0, float y0, float x1, float y1)
{
    int32_t ix0 = (int32_t)floorf(x0) - 2;
    int32_t iy0 = (int32_t)floorf(y0) - 2;
    int32_t ix1 = (int32_t)ceilf(x1) + 2;
    int32_t iy1 = (int32_t)ceilf(y1) + 2;
    if (ix0 < g->x0) g->x0 = ix0;
    if (iy0 < g->y0) g->y0 = iy0;
    if (ix1 > g->x1) g->x1 = ix1;
    if (iy1 > g->y1) g->y1 = iy1;
}

static void setup_ticks(geom_t *g, const eyes_raster_pose_t *p)
{
    g->ticks = p->ticks;
    g->ticks_on = p->ticks > 0.01f;
    if (!g->ticks_on) {
        return;
    }
    float ax = (float)(p->w / 2 + 4);
    float ay = (float)(p->cy - p->h / 2 + 6);
    g->tick = make_seg(ax, ay, ax + 10.0f, ay - 5.0f);
    g->tick_x0 = ax - 3.0f;
    g->tick_x1 = ax + 13.0f;
    g->tick_y0 = ay - 8.0f;
    g->tick_y1 = ay + 3.0f;
    bbox_add(g, -g->tick_x1, g->tick_y0, g->tick_x1, g->tick_y1);
}

static inline bool in_ticks(const geom_t *g, float x, float y)
{
    float ax = fabsf(x);
    return g->ticks_on && ax >= g->tick_x0 && ax <= g->tick_x1 && y >= g->tick_y0 && y <= g->tick_y1;
}

static inline void draw_ticks(const geom_t *g, float x, float y, rgb_t *c)
{
    if (in_ticks(g, x, y)) {
        float d = fast_sqrtf(seg_dist2(&g->tick, fabsf(x), y)) - 2.0f;
        over(c, s_edge, g->ticks * cov(d));
    }
}

static inline float smoothstepf(float e0, float e1, float x)
{
    float t = clampf(0.0f, (x - e0) / (e1 - e0), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static void setup_highlight(geom_t *g)
{
    /* A thick curved gloss streak in the upper-left, on a circle at 68 % of the radius. It is
     * laid out on the round eye and squashed with the body, fading as the eye closes. */
    float hw = g->body.hw;
    float hh = g->body.hh;
    float half = 0.42f;
    g->hl_vs = hw / hh;
    g->hl_px = (hh < hw ? hh : hw) / hw;
    g->hl_rm = 0.68f * hw;
    g->hl_hw = 0.07f * hw > 2.5f ? 0.07f * hw : 2.5f;
    g->hl_cos_half = cosf(half);
    g->hl_mx = -0.66f;
    g->hl_my = -0.75f;
    float c = cosf(half), sn = sinf(half);
    g->hl_e1x = g->hl_rm * (g->hl_mx * c - g->hl_my * sn);
    g->hl_e1y = g->hl_rm * (g->hl_my * c + g->hl_mx * sn);
    g->hl_e2x = g->hl_rm * (g->hl_mx * c + g->hl_my * sn);
    g->hl_e2y = g->hl_rm * (g->hl_my * c - g->hl_mx * sn);
    g->hl_alpha = 0.85f * clampf(0.0f, (hh / hw - 0.35f) / 0.35f, 1.0f);
    /* Feather is 2 px in pixel space; in normalized space that is at most 2 / hl_px. */
    float pad = g->hl_hw + 3.0f / g->hl_px;
    float rmin = g->hl_rm - pad;
    rmin = rmin > 0.0f ? rmin : 0.0f;
    g->hl_r2_min = rmin * rmin;
    g->hl_r2_max = (g->hl_rm + pad) * (g->hl_rm + pad);

    g->glint_x0 = g->glint_y0 = 1e9f;
    g->glint_x1 = g->glint_y1 = -1e9f;
    float reach = g->hl_rm + g->hl_hw + 2.0f;
    for (int k = -8; k <= 8; k++) {
        float ang = atan2f(g->hl_my, g->hl_mx) + half * (float)k / 8.0f;
        for (int side = -1; side <= 1; side += 2) {
            float r = side < 0 ? g->hl_rm - g->hl_hw - 2.0f : reach;
            float x = r * cosf(ang);
            float y = g->body.cy + r * sinf(ang) / g->hl_vs;
            g->glint_x0 = x < g->glint_x0 ? x : g->glint_x0;
            g->glint_x1 = x > g->glint_x1 ? x : g->glint_x1;
            g->glint_y0 = y < g->glint_y0 ? y : g->glint_y0;
            g->glint_y1 = y > g->glint_y1 ? y : g->glint_y1;
        }
    }
    g->glint_x0 -= g->hl_hw + 2.0f;
    g->glint_x1 += g->hl_hw + 2.0f;
    g->glint_y0 -= (g->hl_hw + 2.0f) / g->hl_vs;
    g->glint_y1 += (g->hl_hw + 2.0f) / g->hl_vs;
}

static void setup_dots(geom_t *g)
{
    /* Halftone: rows anchored just above the bottom rim, dots growing toward the bottom. The
     * grid is centered on x = 0 so the pattern stays left/right symmetric (row mirroring). */
    float hw = g->body.hw;
    float hh = g->body.hh;
    float top = g->body.cy - hh;
    float sp = 11.0f * hw / 77.0f;
    g->dot_sp = sp > 7.5f ? sp : 7.5f;
    g->dot_inv_sp = 1.0f / g->dot_sp;
    g->dot_yb = g->body.cy + hh - (ORB_RING_W + ORB_RIM_W);
    g->dot_rows = 0;
    for (int j = 0; j < DOT_ROWS_MAX; j++) {
        float yc = g->dot_yb - ((float)j + 0.5f) * g->dot_sp;
        float t = (yc - top) / (2.0f * hh);
        if (t < 0.5f) {
            break;
        }
        float r = g->dot_sp * (0.08f + 0.34f * smoothstepf(0.5f, 0.95f, t));
        float rin = r - 0.5f > 0.0f ? r - 0.5f : 0.0f;
        g->dot_r[j] = r;
        g->dot_lim2[j] = (r + 0.5f) * (r + 0.5f);
        g->dot_in2[j] = rin * rin;
        g->dot_rows = j + 1;
    }
    g->dot_y0 = g->dot_yb - (float)g->dot_rows * g->dot_sp;
}

static void setup_capsule(geom_t *g, const eyes_raster_pose_t *p)
{
    /* LVGL area { -w/2 .. w/2 } is inclusive, so the continuous half extent is w/2 + 0.5. */
    g->body.hw = (float)(p->w / 2) + 0.5f;
    g->body.hh = (float)(p->h / 2) + 0.5f;
    g->body.cx = 0.0f;
    g->body.cy = (float)p->cy;
    g->grad_y0 = g->body.cy - g->body.hh;
    g->grad_inv = 1.0f / (2.0f * g->body.hh);
    setup_highlight(g);
    setup_dots(g);

    float m = ORB_GLOW_R + 1.0f;
    bbox_add(g, -g->body.hw - m, g->body.cy - g->body.hh - m, g->body.hw + m, g->body.cy + g->body.hh + m);
    g->mirror_x = true; /* the highlight is painted afterwards, see paint_glint() */
    g->asym_x0 = g->asym_x1 = 1e9f;
}

/* Exact unit heart distance; its tip is (0,0), notch (0,1), and lobe radius sqrt(1/8). */
static float heart_sd(float x, float y)
{
    x = fabsf(x);
    if (x + y > 1.0f) {
        float dx = x - 0.25f, dy = y - 0.75f;
        return fast_sqrtf(dx * dx + dy * dy) - 0.35355339f;
    }
    float dy = y - 1.0f;
    float a = x * x + dy * dy;
    float t = fmaxf(x + y, 0.0f) * 0.5f;
    float dx = x - t;
    dy = y - t;
    return fast_sqrtf(fminf(a, dx * dx + dy * dy)) * (x > y ? 1.0f : -1.0f);
}

static void setup_reference(geom_t *g, const eyes_raster_pose_t *p)
{
    eyes_raster_pose_t body = *p;
    if (p->shape == EYES_SHAPE_SLEEP) {
        int32_t lens_h = (int32_t)fmaxf(6.0f, 0.50f * (float)p->w);
        if (body.h > lens_h) body.h = lens_h;
    }
    /* Arches keep the height the expression asks for: Happy, Excited and Sad differ by proportion. */
    setup_capsule(g, &body);
    float tilt = p->shape == EYES_SHAPE_SLOPE ? 0.65f : 0.28f;
    g->lid_slope = p->dir == 0 ? 0.0f : (p->dir > 0 ? tilt : -tilt);
    if (p->shape == EYES_SHAPE_ARCH) g->lid_slope = p->dir < 0 ? -1.0f : 1.0f;
    if (p->shape == EYES_SHAPE_WORRIED) g->lid_slope = p->side < 0 ? -1.0f : 1.0f;
    g->mirror_x = false;
}

static float reference_sd(const geom_t *g, float x, float y)
{
    float hw = g->body.hw, hh = g->body.hh;
    float qy = y - g->body.cy;
    float u = x / hw, v = qy / hh;
    if (g->shape == EYES_SHAPE_OVAL) {
        return (fast_sqrtf(u * u + v * v) - 1.0f) * fminf(hw, hh);
    }
    if (g->shape == EYES_SHAPE_ARCH) {
        /* Elliptical dome minus a broad, shallow underside; smooth the two meeting tips. */
        float outer = (fast_sqrtf(u * u + v * v) - 1.0f) * fminf(hw, hh);
        float by = (qy * g->lid_slope - 0.98f * hh) / (0.95f * hh);
        float bx = x / (1.10f * hw);
        float bite = (fast_sqrtf(bx * bx + by * by) - 1.0f) * fminf(1.10f * hw, 0.95f * hh);
        float rounding = fminf(2.5f, 0.15f * hh);
        float blend = fmaxf(rounding - fabsf(outer + bite), 0.0f) / rounding;
        return fmaxf(outer, -bite) + blend * blend * rounding * 0.25f;
    }
    if (g->shape == EYES_SHAPE_HEART) {
        float sx = hw / 0.60355339f, sy = 2.0f * hh / 1.10355339f;
        return heart_sd(x / sx, (hh - qy) / sy) * fminf(sx, sy);
    }
    if (g->shape == EYES_SHAPE_SLEEP) {
        /* Intersection of equal circles: a shallow lens, pointed at both ends. */
        float r = (hw * hw + hh * hh) / (2.0f * hh);
        float dy = fabsf(qy) + r - hh;
        return fast_sqrtf(x * x + dy * dy) - r;
    }
    if (g->shape == EYES_SHAPE_WORRIED) {
        /* Two joined circles on one baseline; the right one has twice the area (1.41x the radius) of the
         * left. Centres are 1.9 small radii apart, so the pair is 4.31 radii wide and shrinks as a unit. */
        const float k = 1.41421356f;
        float r = fminf(hh / k, hw / 2.157f);
        float rl = k * r;
        float small_x = -1.157f * r, large_x = 0.743f * r;
        if (g->lid_slope < 0.0f) x = -x; /* the left eye is the right eye flipped: big circles face outward */
        float ds = fast_sqrtf((x - small_x) * (x - small_x) + (qy - (rl - r)) * (qy - (rl - r))) - r;
        float dl = fast_sqrtf((x - large_x) * (x - large_x) + qy * qy) - rl;
        float softness = 0.25f * r;
        float blend = fmaxf(softness - fabsf(ds - dl), 0.0f) / softness;
        return fminf(ds, dl) - blend * blend * softness * 0.25f;
    }
    float ellipse = (fast_sqrtf(u * u + v * v) - 1.0f) * fminf(hw, hh);
    float slope = g->lid_slope * hh / hw;
    float lid = (-0.35f * hh + slope * x - qy) / fast_sqrtf(1.0f + slope * slope);
    return fmaxf(ellipse, lid);
}

typedef struct {
    float cx, cy, hw, hh, side;
} accent_box_t;

static bool accent_box(const geom_t *g, const eyes_raster_pose_t *p, accent_box_t *a)
{
    if (p->accent == EYES_ACCENT_NONE || !(p->accent_opacity > 0.0f)) return false;
    a->side = p->side < 0 ? -1.0f : 1.0f;
    float outer = (p->side < 0 ? -(float)g->x0 : (float)g->x1) - 11.0f;
    outer = fminf(outer, (float)p->w * 0.5f);
    a->cx = a->side * (outer + 14.0f);
    a->cy = fmaxf((float)g->y0 + 16.0f, (float)p->cy - (float)p->h * 0.5f + 4.0f);
    a->hw = a->hh = 22.0f;
    if (p->accent == EYES_ACCENT_SIDE_RAYS) a->cy = (float)p->cy;
    if (p->accent == EYES_ACCENT_NERVOUS) {
        /* Chart: three staggered strokes hugging the upper outer edge of the orb. */
        a->cx = a->side * (float)p->w * 0.5f;
        a->cy = (float)p->cy - (float)p->h * 0.475f;
        a->hw = 30.0f;
        a->hh = 62.0f;
    }
    if (p->accent == EYES_ACCENT_BLUSH) {
        a->cx = a->side * (float)p->w * 0.28f;
        a->cy = (float)g->y1 + 3.0f;
        a->hw = 18.0f;
        a->hh = 7.0f;
    }
    /* Move ink, rather than clipping it, when a wide stroke nearly fills the sprite budget. */
    a->cx = clampf((float)g->x1 - 255.0f + a->hw + 2.0f, a->cx,
                   (float)g->x0 + 255.0f - a->hw - 2.0f);
    a->cy = clampf((float)g->y1 - 255.0f + a->hh + 2.0f, a->cy,
                   (float)g->y0 + 255.0f - a->hh - 2.0f);
    return true;
}

static void setup_accent_bounds(geom_t *g, const eyes_raster_pose_t *p)
{
    accent_box_t a;
    if (accent_box(g, p, &a)) {
        bbox_add(g, a.cx - a.hw, a.cy - a.hh, a.cx + a.hw, a.cy + a.hh);
    }
}

static inline float dot_alpha(const geom_t *g, float x, float y)
{
    if (y < g->dot_y0 || y > g->dot_yb) {
        return 0.0f;
    }
    int32_t j = (int32_t)((g->dot_yb - y) * g->dot_inv_sp);
    if (j >= g->dot_rows) {
        return 0.0f;
    }
    float yc = g->dot_yb - ((float)j + 0.5f) * g->dot_sp;
    float fi = x * g->dot_inv_sp;
    int32_t i = (int32_t)(fi + (fi >= 0.0f ? 0.5f : -0.5f));
    float dx = x - (float)i * g->dot_sp;
    float dy = y - yc;
    float r = g->dot_r[j];
    float lim = r + 1.0f;
    float d2 = dx * dx + dy * dy;
    if (d2 >= lim * lim) {
        return 0.0f;
    }
    return ORB_DOT_OPA * cov(fast_sqrtf(d2) - r);
}

/* Interior: gradient row color plus halftone dots. */
static inline uint16_t fill_capsule(const geom_t *g, float x, float y)
{
    int32_t yi = (int32_t)y - g->grad_base;
    int32_t ax = iabs_x(x);
    float a = dot_alpha(g, x, y);
    if (a <= 0.0f) {
        return s_grad_packed[yi][ax & 3];
    }
    rgb_t c = s_grad[yi];
    over(&c, s_orb_dot, a);
    return pack_dither(c, ax, (int32_t)y);
}

static inline uint16_t shade_capsule(const geom_t *g, float x, float y)
{
    float d = sd_stadium_lo(x, y - g->body.cy, g->body.hw, g->body.hh);
    if (d < CAP_LUT_DMIN) {
        return fill_capsule(g, x, y);
    }
    rgb_t c = s_black;
    bool dither = false;
    if (d < CAP_LUT_DMAX) {
        int32_t yi = (int32_t)y - g->grad_base;
        const cap_lut_t *l = &s_cap_lut[(int32_t)((d - CAP_LUT_DMIN) * CAP_LUT_SCALE + 0.5f)];
        const rgb_t *f = &s_grad[yi];
        c.r = l->ar + f->r * l->k;
        c.g = l->ag + f->g * l->k;
        c.b = l->ab + f->b * l->k;
        if (l->k > 0.0f) {
            over(&c, s_orb_dot, dot_alpha(g, x, y) * l->k);
        }
        /* Banding only shows in the smooth fill; the rim, ring and glow don't need dither. */
        dither = l->k > 0.5f;
    } else if (!g->ticks_on) {
        return 0;
    }
    draw_ticks(g, x, y, &c);
    return dither ? pack_dither(c, iabs_x(x), (int32_t)y) : pack(c);
}

static void build_cap_lut(void)
{
    for (int i = 0; i < CAP_LUT_N; i++) {
        float d = CAP_LUT_DMIN + (float)i / CAP_LUT_SCALE;
        /* Outside in: soft bloom, thin bright ring, dark band, then the fill with a soft inner
         * edge and a shadow that fades in from the rim. */
        float bloom = 1.0f - smoothstepf(0.0f, ORB_GLOW_R, d);
        rgb_t c = s_black;
        over(&c, s_orb_glow, 0.75f * bloom * bloom * bloom);
        over(&c, s_orb_ring, cov(d));
        over(&c, s_orb_rim, cov(d + ORB_RING_W));
        float fill = clampf(0.0f, 0.5f - (d + ORB_RING_W + ORB_RIM_W) / 1.5f, 1.0f);
        float shadow = 0.3f * smoothstepf(CAP_LUT_DMIN, -(ORB_RING_W + ORB_RIM_W), d);
        s_cap_lut[i].ar = c.r * (1.0f - fill);
        s_cap_lut[i].ag = c.g * (1.0f - fill);
        s_cap_lut[i].ab = c.b * (1.0f - fill);
        s_cap_lut[i].k = fill * (1.0f - shadow);
    }
}

static void build_gradient(geom_t *g)
{
    g->grad_base = g->y0;
    int32_t n = g->y1 - g->y0 + 1;
    if (n > GRAD_MAX) {
        n = GRAD_MAX;
    }
    for (int32_t i = 0; i < n; i++) {
        float t = clampf(0.0f, ((float)(g->y0 + i) - g->grad_y0) * g->grad_inv, 1.0f);
        rgb_t a = t < ORB_FILL_MID_T ? s_orb_top : s_orb_mid;
        rgb_t b = t < ORB_FILL_MID_T ? s_orb_mid : s_orb_bottom;
        float u = t < ORB_FILL_MID_T ? t / ORB_FILL_MID_T : (t - ORB_FILL_MID_T) / (1.0f - ORB_FILL_MID_T);
        u = u * u * (3.0f - 2.0f * u);
        rgb_t c = { a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u };
        s_grad[i] = c;
        rgb_t cd = c;
        over(&cd, s_orb_dot, ORB_DOT_OPA);
        for (int32_t k = 0; k < 4; k++) {
            s_grad_packed[i][k] = pack_dither(c, k, g->y0 + i);
            s_grad_dot_packed[i][k] = pack_dither(cd, k, g->y0 + i);
        }
    }
}

/* Orb material on any shape from its signed distance d (negative inside): bloom, bright ring,
 * dark rim and inner shadow from the shared table, filled with the orb gradient row. */
static inline uint16_t shade_orb_d(const geom_t *g, float d, float x, float y)
{
    bool tick = g->ticks_on && in_ticks(g, x, y);
    if (d >= CAP_LUT_DMAX && !tick) {
        return 0;
    }
    int32_t yi = (int32_t)y - g->grad_base;
    int32_t ax = iabs_x(x);
    rgb_t c = s_black;
    bool dither = false;
    if (d < CAP_LUT_DMIN) {
        if (!tick) {
            return s_grad_packed[yi][ax & 3];
        }
        c = s_grad[yi];
        dither = true;
    } else if (d < CAP_LUT_DMAX) {
        const cap_lut_t *l = &s_cap_lut[(int32_t)((d - CAP_LUT_DMIN) * CAP_LUT_SCALE + 0.5f)];
        const rgb_t *f = &s_grad[yi];
        c.r = l->ar + f->r * l->k;
        c.g = l->ag + f->g * l->k;
        c.b = l->ab + f->b * l->k;
        dither = l->k > 0.5f;
    }
    if (tick) {
        draw_ticks(g, x, y, &c);
    }
    return dither ? pack_dither(c, ax, (int32_t)y) : pack(c);
}

static inline rgb_t unpack(uint16_t px);

static inline uint16_t shade_reference(const geom_t *g, float x, float y)
{
    float d = reference_sd(g, x, y);
    uint16_t px = shade_orb_d(g, d, x, y);
    if (d < -(ORB_RING_W + ORB_RIM_W + 1.0f)) {
        rgb_t c = unpack(px);
        float inner = cov(d + ORB_RING_W + ORB_RIM_W + 2.0f);
        over(&c, s_orb_dot, inner * dot_alpha(g, x, y));
        float u = (x + 0.32f * g->body.hw) / (0.32f * g->body.hw);
        float v = (y - g->body.cy + 0.08f * g->body.hh) / (0.09f * g->body.hh + 1.0f);
        float gloss = clampf(0.0f, 1.0f - u * u - v * v, 1.0f);
        if (g->shape != EYES_SHAPE_OVAL) over(&c, s_white, 0.7f * inner * gloss);
        px = pack_dither(c, iabs_x(x), (int32_t)y);
    }
    return px;
}

static void setup_chevron(geom_t *g, const eyes_raster_pose_t *p)
{
    int32_t stroke = (int32_t)(p->stroke_basis * 0.3f);
    if (stroke < 8) {
        stroke = 8;
    }
    float half_w = (float)(p->w / 2);
    float half_h = (float)(p->h / 2);
    float sgn = p->dir >= 0 ? 1.0f : -1.0f;
    float cy = (float)p->cy;
    g->chev_a = make_seg(-half_w * sgn, cy - half_h, half_w * sgn, cy);
    g->chev_b = make_seg(half_w * sgn, cy, -half_w * sgn, cy + half_h);
    g->half_main = (float)stroke / 2.0f;
    float m = g->half_main + CAP_LUT_DMAX + 1.0f;
    bbox_add(g, -half_w - m, cy - half_h - m, half_w + m, cy + half_h + m);
    g->grad_y0 = cy - half_h - g->half_main;
    g->grad_inv = 1.0f / (2.0f * (half_h + g->half_main));
}

static inline float chevron_d2(const geom_t *g, float x, float y)
{
    float d2a = seg_dist2(&g->chev_a, x, y);
    float d2b = seg_dist2(&g->chev_b, x, y);
    return d2a < d2b ? d2a : d2b;
}

/* Squared distance to the spine goes through the q^2 table (built with cap_r = half stroke). */
static inline uint16_t shade_chevron(const geom_t *g, float x, float y)
{
    float q2 = chevron_d2(g, x, y);
    if (g->ticks_on && in_ticks(g, x, y)) {
        return shade_orb_d(g, fast_sqrtf(q2) - g->half_main, x, y);
    }
    int32_t yi = (int32_t)y - g->grad_base;
    if (q2 < g->q2_min) {
        return s_grad_packed[yi][iabs_x(x) & 3];
    }
    int32_t k = (int32_t)((q2 - g->q2_min) * g->q2_inv_step);
    if (k >= g->q2_n) {
        return 0;
    }
    const cap_lut_t *l = &s_cap_lut[s_q2_lut[k]];
    const rgb_t *f = &s_grad[yi];
    rgb_t c = { l->ar + f->r * l->k, l->ag + f->g * l->k, l->ab + f->b * l->k };
    return l->k > 0.5f ? pack_dither(c, iabs_x(x), (int32_t)y) : pack(c);
}

static void setup_crescent(geom_t *g, const eyes_raster_pose_t *p)
{
    int32_t radius = p->w / 2;
    int32_t radius_y = (int32_t)lroundf((float)radius * clampf(0.1f, p->squash, 1.15f));
    g->body.hw = (float)radius + 0.5f;
    g->body.hh = (float)radius_y + 0.5f;
    g->body.cx = 0.0f;
    g->body.cy = (float)p->cy;
    g->bite = g->body;
    if (p->dir == 0) {
        g->bite.cy = (float)(p->cy - (int32_t)((float)radius_y * 0.55f));
    } else if (p->dir > 0) {
        g->bite.cx = (float)(-(int32_t)((float)radius * 0.7f));
    } else {
        g->bite.cx = (float)(int32_t)((float)radius * 0.7f);
    }
    g->grad_y0 = g->body.cy - g->body.hh;
    g->grad_inv = 1.0f / (2.0f * g->body.hh);
    float m = ORB_GLOW_R + 1.0f;
    bbox_add(g, -g->body.hw - m, g->body.cy - g->body.hh - m, g->body.hw + m, g->body.cy + g->body.hh + m);
    g->mirror_x = p->dir == 0;
    g->asym_x0 = g->asym_x1 = 1e9f;
}

/* Same orb shading as the capsule (bloom, ring, rim, gradient), with the bite painted black over
 * everything, glow included, so it reads as a lid cutting the eye. */
static inline uint16_t shade_crescent(const geom_t *g, float x, float y)
{
    float d = sd_stadium(x - g->body.cx, y - g->body.cy, g->body.hw, g->body.hh);
    if (d >= CAP_LUT_DMAX && !g->ticks_on) {
        return 0;
    }
    rgb_t c = s_black;
    if (d < CAP_LUT_DMAX) {
        int32_t yi = (int32_t)y - g->grad_base;
        if (d < CAP_LUT_DMIN) {
            c = s_grad[yi];
        } else {
            const cap_lut_t *l = &s_cap_lut[(int32_t)((d - CAP_LUT_DMIN) * CAP_LUT_SCALE + 0.5f)];
            c.r = l->ar + s_grad[yi].r * l->k;
            c.g = l->ag + s_grad[yi].g * l->k;
            c.b = l->ab + s_grad[yi].b * l->k;
        }
        float db = sd_stadium(x - g->bite.cx, y - g->bite.cy, g->bite.hw, g->bite.hh);
        over(&c, s_black, cov(db));
    }
    draw_ticks(g, x, y, &c);
    return pack_dither(c, iabs_x(x), (int32_t)y);
}

/* atan2 to ~1e-5 rad with one division; libm's is far slower on this core. */
static inline float fast_atan2f(float y, float x)
{
    float ax = fabsf(x);
    float ay = fabsf(y);
    float mx = ax > ay ? ax : ay;
    float mn = ax > ay ? ay : ax;
    float a = mx > 0.0f ? mn / mx : 0.0f;
    float s2 = a * a;
    float r = ((-0.0464964749f * s2 + 0.15931422f) * s2 - 0.327622764f) * s2 * a + a;
    if (ay > ax) {
        r = 1.57079637f - r;
    }
    if (x < 0.0f) {
        r = 3.14159274f - r;
    }
    return y < 0.0f ? -r : r;
}

static void setup_spiral(geom_t *g, const eyes_raster_pose_t *p)
{
    setup_capsule(g, p);
    g->dot_rows = 0; /* the spiral replaces the halftone */
    g->dot_y0 = g->dot_yb = -1e9f;
    g->mirror_x = false;
    g->spin_dir = p->dir >= 0 ? 1.0f : -1.0f;
    g->spin_phase = p->spin;
    g->spiral_amt = p->stripes;
    float pitch = 0.42f * g->body.hw;
    g->spiral_pitch = pitch > 10.0f ? pitch : 10.0f;
    g->spiral_inv_pitch = 1.0f / g->spiral_pitch;
}

/* Interior: gradient with two dark spiral arms; s counts arm periods, so the stripe repeats
 * every pitch radially and phase shifts spin it. */
static inline uint16_t fill_spiral(const geom_t *g, float x, float y)
{
    int32_t yi = (int32_t)y - g->grad_base;
    int32_t ax = iabs_x(x);
    if (g->spiral_amt <= 0.0f) {
        return s_grad_packed[yi][ax & 3];
    }
    float qy = y - g->body.cy;
    float r = fast_sqrtf(x * x + qy * qy);
    float fade = g->spiral_amt * smoothstepf(2.0f, 10.0f, r);
    if (fade <= 0.0f) {
        return s_grad_packed[yi][ax & 3];
    }
    float s = g->spin_dir * fast_atan2f(qy, x) * (SPIRAL_ARMS / 6.28318531f) + r * g->spiral_inv_pitch -
              g->spin_phase + 4.0f;
    float f = s - (float)(int32_t)s;
    float edge_px = (SPIRAL_HALF - fabsf(f - 0.5f)) * g->spiral_pitch;
    float a = SPIRAL_OPA * fade * clampf(0.0f, 0.5f + edge_px, 1.0f);
    if (a <= 0.0f) {
        return s_grad_packed[yi][ax & 3];
    }
    rgb_t c = s_grad[yi];
    over(&c, s_spiral, a);
    return pack_dither(c, ax, (int32_t)y);
}

static inline uint16_t shade_spiral(const geom_t *g, float x, float y)
{
    float d = sd_stadium_lo(x, y - g->body.cy, g->body.hw, g->body.hh);
    if (d < CAP_LUT_DMIN && !(g->ticks_on && in_ticks(g, x, y))) {
        return fill_spiral(g, x, y);
    }
    return shade_orb_d(g, d, x, y);
}

/* Block culling: every distance below is 1-Lipschitz, so one sample at an 8x8 block's center
 * classifies the whole block when it's farther than the block's half-diagonal from any edge. */
#define BLOCK      8
#define BLOCK_REACH 6.7f /* half-diagonal of an 8x8 block plus AA margin */

static inline float outer_capsule(const geom_t *g, float x, float y)
{
    return sd_stadium(x, y - g->body.cy, g->body.hw, g->body.hh) - ORB_GLOW_R - 0.5f;
}

static inline float outer_chevron(const geom_t *g, float x, float y)
{
    return fast_sqrtf(chevron_d2(g, x, y)) - g->half_main - CAP_LUT_DMAX - 0.5f;
}

static inline float outer_crescent(const geom_t *g, float x, float y)
{
    return sd_stadium(x - g->body.cx, y - g->body.cy, g->body.hw, g->body.hh) - ORB_GLOW_R - 0.5f;
}

#define outer_spiral outer_capsule

static inline float outer_reference(const geom_t *g, float x, float y)
{
    return reference_sd(g, x, y) - ORB_GLOW_R - 0.5f;
}

/* Only orbs have a solid interior worth a fast path; elsewhere this never matches. Reference
 * shapes blend/round several signed distances (see reference_sd()) rather than measuring one
 * exact one, so unlike sd_stadium a block's worst-case error over BLOCK_REACH isn't bounded
 * (confirmed by reference_rotations() in test_eyes_raster.c breaking once this was tried) -
 * they're restricted to the background-culling fast path above, like CHEVRON/CRESCENT. */
static inline bool block_interior(const geom_t *g, float x, float y)
{
    if (g->shape == EYES_SHAPE_CAPSULE || g->shape == EYES_SHAPE_SPIRAL) {
        return sd_stadium(x, y - g->body.cy, g->body.hw, g->body.hh) < CAP_LUT_DMIN - BLOCK_REACH;
    }
    return false;
}

static inline bool block_near_ticks(const geom_t *g, float x, float y)
{
    float ax = fabsf(x);
    return g->ticks_on && ax >= g->tick_x0 - BLOCK_REACH && ax <= g->tick_x1 + BLOCK_REACH &&
           y >= g->tick_y0 - BLOCK_REACH && y <= g->tick_y1 + BLOCK_REACH;
}

/* Panel-space (du, dv) offsets from the eye center map to logical (x, y) through the inverse
 * rotation: x = mxx*du + mxy*dv, y = myx*du + myy*dv. */
#define RASTER_BANDS(SHADE, OUTER, FILL)                                                      \
    for (int32_t bv = 0; bv < s->h; bv += BLOCK) {                                           \
        int32_t ve = bv + BLOCK < s->h ? bv + BLOCK : s->h;                                  \
        for (int32_t bu = 0; bu < s->w; bu += BLOCK) {                                       \
            int32_t ue = bu + BLOCK < s->w ? bu + BLOCK : s->w;                              \
            float cu = (float)s->ox + (float)(bu + ue - 1) * 0.5f;                           \
            float cv = (float)s->oy + (float)(bv + ve - 1) * 0.5f;                           \
            float xc = mxx * cu + mxy * cv;                                                  \
            float yc = myx * cu + myy * cv;                                                  \
            int kind = 2; /* 0 black, 1 interior, 2 per-pixel */                             \
            if (OUTER(&g, xc, yc) > BLOCK_REACH && !block_near_ticks(&g, xc, yc)) {          \
                kind = 0;                                                                    \
            } else if (block_interior(&g, xc, yc)) {                                         \
                kind = 1;                                                                    \
            }                                                                                \
            for (int32_t v = bv; v < ve; v++) {                                              \
                if (row_mirror && mirror_src(&g, s, mxy, v) >= 0) {                          \
                    continue;                                                                \
                }                                                                            \
                uint16_t *row = s->px + v * s->w;                                            \
                if (kind == 0) {                                                             \
                    memset(row + bu, 0, (size_t)(ue - bu) * sizeof(uint16_t));               \
                    continue;                                                                \
                }                                                                            \
                float dv = (float)(s->oy + v);                                               \
                float du = (float)(s->ox + bu);                                              \
                float x = mxx * du + mxy * dv;                                               \
                float y = myx * du + myy * dv;                                               \
                if (kind == 1) {                                                             \
                    for (int32_t u = bu; u < ue; u++, x += mxx, y += myx) {                  \
                        row[u] = FILL(&g, x, y);                                             \
                    }                                                                        \
                } else {                                                                     \
                    for (int32_t u = bu; u < ue; u++, x += mxx, y += myx) {                  \
                        row[u] = SHADE(&g, x, y);                                            \
                    }                                                                        \
                }                                                                            \
            }                                                                                \
        }                                                                                    \
    }

/* With a 90/270 rotation each sprite row is one logical column x. For x-symmetric shapes a
 * row with x > 0 equals the row at -x, unless either side touches the asymmetric range.
 * Returns that source row, or -1 if this row must be shaded. */
static inline int32_t mirror_src(const geom_t *g, const eyes_sprite_t *s, float mxy, int32_t v)
{
    int32_t dv = s->oy + v;
    float x = mxy * (float)dv;
    if (x <= 0.0f) {
        return -1;
    }
    if ((x >= g->asym_x0 && x <= g->asym_x1) || (-x >= g->asym_x0 && -x <= g->asym_x1)) {
        return -1;
    }
    int32_t src = -dv - s->oy;
    return (src >= 0 && src < s->h) ? src : -1;
}

static void clip_span(int32_t *lo, int32_t *hi, int32_t cap)
{
    int32_t excess = (*hi - *lo + 1) - cap;
    if (excess > 0) {
        *lo += excess / 2;
        *hi = *lo + cap - 1;
    }
}

/* Maps a logical rect (relative to the eye center) to panel offsets, and returns the inverse
 * mapping coefficients: x = mxx*du + mxy*dv, y = myx*du + myy*dv. */
static void rotate_rect(int rotation, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t *du0,
                        int32_t *du1, int32_t *dv0, int32_t *dv1, float m[4])
{
    switch (rotation) {
    case 90:
        *du0 = -y1; *du1 = -y0; *dv0 = x0; *dv1 = x1;
        m[0] = 0.0f; m[1] = 1.0f; m[2] = -1.0f; m[3] = 0.0f;
        break;
    case 180:
        *du0 = -x1; *du1 = -x0; *dv0 = -y1; *dv1 = -y0;
        m[0] = -1.0f; m[1] = 0.0f; m[2] = 0.0f; m[3] = -1.0f;
        break;
    case 270:
        *du0 = y0; *du1 = y1; *dv0 = -x1; *dv1 = -x0;
        m[0] = 0.0f; m[1] = -1.0f; m[2] = 1.0f; m[3] = 0.0f;
        break;
    default:
        *du0 = x0; *du1 = x1; *dv0 = y0; *dv1 = y1;
        m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f; m[3] = 1.0f;
        break;
    }
}

static inline rgb_t unpack(uint16_t px)
{
    uint16_t v = (uint16_t)((px >> 8) | (px << 8));
    rgb_t c = { (float)((v >> 11) & 0x1F) * (255.0f / 31.0f), (float)((v >> 5) & 0x3F) * (255.0f / 63.0f),
                (float)(v & 0x1F) * (255.0f / 31.0f) };
    return c;
}

static inline float highlight_alpha(const geom_t *g, float x, float y)
{
    float u = x;
    float v = (y - g->body.cy) * g->hl_vs;
    float q2 = u * u + v * v;
    if (q2 < g->hl_r2_min || q2 > g->hl_r2_max) {
        return 0.0f;
    }
    float len = fast_sqrtf(q2);
    float nd;
    if (u * g->hl_mx + v * g->hl_my >= len * g->hl_cos_half) {
        nd = fabsf(len - g->hl_rm);
    } else {
        float d1x = u - g->hl_e1x, d1y = v - g->hl_e1y;
        float d2x = u - g->hl_e2x, d2y = v - g->hl_e2y;
        float a2 = d1x * d1x + d1y * d1y;
        float b2 = d2x * d2x + d2y * d2y;
        nd = fast_sqrtf(a2 < b2 ? a2 : b2);
    }
    float pd = (nd - g->hl_hw) * g->hl_px;
    /* 2 px feather so the streak reads as soft gloss, not a hard stroke. */
    return g->hl_alpha * clampf(0.0f, 0.5f - pd / 2.0f, 1.0f);
}

/* The capsule's glossy highlight breaks left/right symmetry, so it's blended onto the finished
 * sprite over its small bounding box only. */
static void paint_glint(const geom_t *g, int rotation, eyes_sprite_t *s)
{
    if (g->hl_alpha <= 0.0f) {
        return;
    }
    int32_t du0, du1, dv0, dv1;
    float m[4];
    rotate_rect(rotation, (int32_t)floorf(g->glint_x0), (int32_t)floorf(g->glint_y0), (int32_t)ceilf(g->glint_x1),
                (int32_t)ceilf(g->glint_y1), &du0, &du1, &dv0, &dv1, m);
    int32_t u0 = du0 - s->ox, u1 = du1 - s->ox, v0 = dv0 - s->oy, v1 = dv1 - s->oy;
    u0 = u0 < 0 ? 0 : u0;
    v0 = v0 < 0 ? 0 : v0;
    u1 = u1 >= s->w ? s->w - 1 : u1;
    v1 = v1 >= s->h ? s->h - 1 : v1;
    for (int32_t v = v0; v <= v1; v++) {
        for (int32_t u = u0; u <= u1; u++) {
            float du = (float)(s->ox + u);
            float dv = (float)(s->oy + v);
            float x = m[0] * du + m[1] * dv;
            float y = m[2] * du + m[3] * dv;
            float a = highlight_alpha(g, x, y);
            if (a > 0.0f) {
                uint16_t *px = &s->px[v * s->w + u];
                rgb_t c = unpack(*px);
                over(&c, s_white, a);
                *px = pack(c);
            }
        }
    }
}

typedef struct {
    geom_t g;
    eyes_sprite_t *s;
    float mxx, mxy, myx, myy;
    bool row_mirror;
    int rotation;
} raster_job_t;

static raster_job_t s_job;

/* |y - cy| below which the stadium distance is < D for a column at |x| = ax, or -1 if none. */
static inline float stadium_span(const geom_t *g, float ax, float D)
{
    float hw = g->body.hw;
    float hh = g->body.hh;
    if (hh >= hw) {
        float r = hw + D;
        if (r <= 0.0f || ax >= r) {
            return -1.0f;
        }
        return (hh - hw) + fast_sqrtf(r * r - ax * ax);
    }
    float dx = ax - (hw - hh);
    dx = dx > 0.0f ? dx : 0.0f;
    float r = hh + D;
    if (r <= 0.0f || dx >= r) {
        return -1.0f;
    }
    return fast_sqrtf(r * r - dx * dx);
}

/* q^2 -> s_cap_lut index for d = q - r: the capsule's cap radius, or a chevron's half stroke. */
static void build_q2_lut(geom_t *g, float r)
{
    g->cap_r = r;
    float rmin = g->cap_r + CAP_LUT_DMIN;
    rmin = rmin > 0.0f ? rmin : 0.0f;
    float rmax = g->cap_r + CAP_LUT_DMAX;
    g->q2_min = rmin * rmin;
    /* A q2 step of 0.1 * r keeps the distance error <= 0.05 px at the inner edge (dq = dq2 / 2q). */
    float step = 0.1f * rmin > 0.2f ? 0.1f * rmin : 0.2f;
    float span = rmax * rmax - g->q2_min;
    if (span / step > (float)(Q2_MAX - 2)) {
        step = span / (float)(Q2_MAX - 2);
    }
    g->q2_inv_step = 1.0f / step;
    g->q2_n = (int32_t)(span / step) + 2;
    for (int32_t k = 0; k < g->q2_n; k++) {
        float d = fast_sqrtf(g->q2_min + ((float)k + 0.5f) * step) - g->cap_r;
        int32_t idx = (int32_t)((d - CAP_LUT_DMIN) * CAP_LUT_SCALE + 0.5f);
        s_q2_lut[k] = (uint16_t)(idx < 0 ? 0 : (idx >= CAP_LUT_N ? CAP_LUT_N - 1 : idx));
    }
}

/* Halftone for an interior pixel of a column whose nearest dot column is dx2 away (squared). */
static inline uint16_t fill_column(const geom_t *g, float dx2, float y, int32_t yi, int32_t dither_x)
{
    if (y >= g->dot_y0 && y <= g->dot_yb) {
        int32_t j = (int32_t)((g->dot_yb - y) * g->dot_inv_sp);
        if (j < g->dot_rows) {
            float dy = y - (g->dot_yb - ((float)j + 0.5f) * g->dot_sp);
            float d2 = dx2 + dy * dy;
            if (d2 <= g->dot_in2[j]) {
                return s_grad_dot_packed[yi][dither_x];
            }
            if (d2 < g->dot_lim2[j]) {
                rgb_t c = s_grad[yi];
                over(&c, s_orb_dot, ORB_DOT_OPA * cov(fast_sqrtf(d2) - g->dot_r[j]));
                return pack_dither(c, dither_x, (int32_t)y);
            }
        }
    }
    return s_grad_packed[yi][dither_x];
}

/* Edge band pixel given its precomputed shading-table entry. */
static inline uint16_t shade_band(const geom_t *g, float x, float y, const cap_lut_t *l, int32_t dither_x)
{
    int32_t yi = (int32_t)y - g->grad_base;
    const rgb_t *f = &s_grad[yi];
    rgb_t c = { l->ar + f->r * l->k, l->ag + f->g * l->k, l->ab + f->b * l->k };
    if (l->k <= 0.0f) {
        return pack(c);
    }
    over(&c, s_orb_dot, dot_alpha(g, x, y) * l->k);
    return l->k > 0.5f ? pack_dither(c, dither_x, (int32_t)y) : pack(c);
}

/* u range [u0, u1) within [0, w) where |y0 + myx * u - cy| < half (myx = +-1). Matches the
 * per-pixel test exactly: the float estimate is nudged at both ends. */
static void span_of(float cy, float half, float y0, float myx, int32_t w, int32_t *u0, int32_t *u1)
{
    if (half <= 0.0f) {
        *u0 = *u1 = 0;
        return;
    }
    /* Solve for the two u where the test flips, then nudge to the exact integer boundary. */
    float ua = (cy - half - y0) * myx;
    float ub = (cy + half - y0) * myx;
    float lo = ua < ub ? ua : ub;
    int32_t a = (int32_t)floorf(lo) - 1;
    a = a < 0 ? 0 : (a > w ? w : a);
    while (a < w && !(fabsf(y0 + myx * (float)a - cy) < half)) {
        a++;
    }
    while (a > 0 && fabsf(y0 + myx * (float)(a - 1) - cy) < half) {
        a--;
    }
    int32_t b = a;
    while (b < w && fabsf(y0 + myx * (float)b - cy) < half) {
        b++;
    }
    *u0 = a;
    *u1 = b;
}

/* u range [u0, u1) within [0, w) where lo <= y0 + myx * u <= hi. */
static void span_of_range(float lo, float hi, float y0, float myx, int32_t w, int32_t *u0, int32_t *u1)
{
    float ua = (lo - y0) * myx;
    float ub = (hi - y0) * myx;
    float m = ua < ub ? ua : ub;
    int32_t a = (int32_t)floorf(m) - 1;
    a = a < 0 ? 0 : (a > w ? w : a);
    while (a < w) {
        float y = y0 + myx * (float)a;
        if (y >= lo && y <= hi) {
            break;
        }
        a++;
    }
    int32_t b = a;
    while (b < w) {
        float y = y0 + myx * (float)b;
        if (!(y >= lo && y <= hi)) {
            break;
        }
        b++;
    }
    *u0 = a;
    *u1 = b;
}

/* Capsule fast path when sprite rows are logical columns (90/270): per row, the pure-interior
 * and pure-black spans are solved exactly, the edge band is shaded through a squared-distance
 * table (no sqrt), and each finished row is copied to its mirror while still in cache. */
static void raster_capsule_columns(const raster_job_t *job)
{
    const geom_t *g = &job->g;
    eyes_sprite_t *s = job->s;
    const float mxy = job->mxy, myx = job->myx;
    const float hw = g->body.hw, hh = g->body.hh;
    for (int32_t v = 0; v < s->h; v++) {
        if (job->row_mirror && mirror_src(g, s, mxy, v) >= 0) {
            continue;
        }
        uint16_t *row = s->px + v * s->w;
        float x = mxy * (float)(s->oy + v);
        float ax = fabsf(x);
        int32_t dither_x = (int32_t)ax & 3;
        float inner = stadium_span(g, ax, CAP_LUT_DMIN);
        bool tick_col = g->ticks_on && ax >= g->tick_x0 && ax <= g->tick_x1;
        float outer = tick_col ? 1e9f : stadium_span(g, ax, CAP_LUT_DMAX);
        float fi = x * g->dot_inv_sp;
        float ddx = x - (float)(int32_t)(fi + (fi >= 0.0f ? 0.5f : -0.5f)) * g->dot_sp;
        float dx2 = ddx * ddx;
        /* Stadium reduced to its cap circle along this column. */
        float qx = hh >= hw ? ax : ax - (hw - hh);
        qx = qx > 0.0f ? qx : 0.0f;
        float qx2 = qx * qx;
        float straight = hh >= hw ? hh - hw : 0.0f;
        /* y(u) = y0 + myx * u is monotonic, so each class is one contiguous span of u. */
        const float y0 = myx * (float)s->ox;
        int32_t in0, in1, out0, out1; /* [in0, in1) interior, [out0, out1) interior or band */
        span_of(g->body.cy, inner, y0, myx, s->w, &in0, &in1);
        span_of(g->body.cy, outer, y0, myx, s->w, &out0, &out1);
        if (in0 >= in1) {
            in0 = in1 = out0;
        }
        memset(row, 0, (size_t)out0 * sizeof(uint16_t));
        memset(row + out1, 0, (size_t)(s->w - out1) * sizeof(uint16_t));
        for (int32_t pass = 0; pass < 2; pass++) {
            int32_t u0 = pass == 0 ? out0 : in1;
            int32_t u1 = pass == 0 ? in0 : out1;
            float y = y0 + myx * (float)u0;
            for (int32_t u = u0; u < u1; u++, y += myx) {
                if (tick_col) {
                    row[u] = shade_capsule(g, x, y);
                    continue;
                }
                float qy = fabsf(y - g->body.cy) - straight;
                qy = qy > 0.0f ? qy : 0.0f;
                int32_t k = (int32_t)((qx2 + qy * qy - g->q2_min) * g->q2_inv_step);
                k = k < 0 ? 0 : (k >= g->q2_n ? g->q2_n - 1 : k);
                row[u] = shade_band(g, x, y, &s_cap_lut[s_q2_lut[k]], dither_x);
            }
        }
        /* Interior: plain gradient rows, halftone only inside the dot band. */
        int32_t d0 = in1, d1 = in1;
        if (g->dot_rows > 0) {
            span_of_range(g->dot_y0, g->dot_yb, y0, myx, s->w, &d0, &d1);
            d0 = d0 < in0 ? in0 : (d0 > in1 ? in1 : d0);
            d1 = d1 < d0 ? d0 : (d1 > in1 ? in1 : d1);
        }
        int32_t base = (int32_t)y0 - g->grad_base;
        int32_t step = (int32_t)myx;
        for (int32_t u = in0; u < in1; u++) {
            if (u == d0 && d0 < d1) {
                float y = y0 + myx * (float)u;
                for (; u < d1; u++, y += myx) {
                    row[u] = fill_column(g, dx2, y, (int32_t)y - g->grad_base, dither_x);
                }
                if (u >= in1) {
                    break;
                }
            }
            row[u] = s_grad_packed[base + step * u][dither_x];
        }
        if (job->row_mirror && x < 0.0f) {
            int32_t dst = -(s->oy + v) - s->oy;
            if (dst >= 0 && dst < s->h && mirror_src(g, s, mxy, dst) == v) {
                memcpy(s->px + dst * s->w, row, (size_t)s->w * sizeof(uint16_t));
            }
        }
    }
}

static void raster_shape(raster_job_t *job)
{
    const geom_t g = job->g;
    eyes_sprite_t *s = job->s;
    const float mxx = job->mxx, mxy = job->mxy, myx = job->myx, myy = job->myy;
    const bool row_mirror = job->row_mirror;
    if (g.shape == EYES_SHAPE_CAPSULE && mxx == 0.0f) {
        raster_capsule_columns(job);
        return;
    }
    switch (g.shape) {
    case EYES_SHAPE_CAPSULE: RASTER_BANDS(shade_capsule, outer_capsule, fill_capsule); break;
    case EYES_SHAPE_CHEVRON: RASTER_BANDS(shade_chevron, outer_chevron, fill_capsule); break;
    case EYES_SHAPE_CRESCENT: RASTER_BANDS(shade_crescent, outer_crescent, fill_capsule); break;
    case EYES_SHAPE_SPIRAL: RASTER_BANDS(shade_spiral, outer_spiral, fill_spiral); break;
    case EYES_SHAPE_ARCH:
    case EYES_SHAPE_HALF:
    case EYES_SHAPE_SLOPE:
    case EYES_SHAPE_HEART:
    case EYES_SHAPE_WORRIED:
    case EYES_SHAPE_SLEEP:
    case EYES_SHAPE_OVAL:
        RASTER_BANDS(shade_reference, outer_reference, fill_capsule);
        break;
    }
}

/* Copies mirrored rows from their (already shaded) sources. */
static void mirror_rows(raster_job_t *job)
{
    eyes_sprite_t *s = job->s;
    for (int32_t v = 0; v < s->h; v++) {
        int32_t src = mirror_src(&job->g, s, job->mxy, v);
        if (src >= 0) {
            memcpy(s->px + v * s->w, s->px + src * s->w, (size_t)s->w * sizeof(uint16_t));
        }
    }
}

static void setup_geom(geom_t *g, const eyes_raster_pose_t *pose)
{
    memset(g, 0, sizeof(*g));
    g->shape = pose->shape;
    g->x0 = g->y0 = INT32_MAX;
    g->x1 = g->y1 = INT32_MIN;
    switch (pose->shape) {
    case EYES_SHAPE_CAPSULE: setup_capsule(g, pose); break;
    case EYES_SHAPE_CHEVRON: setup_chevron(g, pose); break;
    case EYES_SHAPE_CRESCENT: setup_crescent(g, pose); break;
    case EYES_SHAPE_SPIRAL: setup_spiral(g, pose); break;
    case EYES_SHAPE_ARCH:
    case EYES_SHAPE_HALF:
    case EYES_SHAPE_SLOPE:
    case EYES_SHAPE_HEART:
    case EYES_SHAPE_WORRIED:
    case EYES_SHAPE_SLEEP:
    case EYES_SHAPE_OVAL: setup_reference(g, pose); break;
    }
    setup_ticks(g, pose);
    setup_accent_bounds(g, pose);
}

void eyes_raster_bounds(const eyes_raster_pose_t *pose, int rotation, int32_t *w, int32_t *h)
{
    geom_t g;
    setup_geom(&g, pose);
    int32_t du0, du1, dv0, dv1;
    float m[4];
    rotate_rect(rotation, g.x0, g.y0, g.x1, g.y1, &du0, &du1, &dv0, &dv1, m);
    *w = du1 - du0 + 1;
    *h = dv1 - dv0 + 1;
}

static void ensure_colors(void)
{
    static bool colors_ready;
    if (!colors_ready) {
        s_edge = hex(COLOR_EDGE);
        s_orb_top = hex(ORB_FILL_TOP);
        s_orb_mid = hex(ORB_FILL_MID);
        s_orb_bottom = hex(ORB_FILL_BOTTOM);
        s_orb_rim = hex(ORB_RIM);
        s_orb_ring = hex(ORB_RING);
        s_orb_glow = hex(ORB_GLOW);
        s_orb_dot = hex(ORB_DOT);
        s_spiral = hex(SPIRAL_COLOR);
        build_cap_lut();
        colors_ready = true;
    }
}

static void render_full(const eyes_raster_pose_t *pose, int rotation, eyes_sprite_t *s, bool gloss)
{
    raster_job_t *job = &s_job;
    geom_t *g = &job->g;
    setup_geom(g, pose);
    build_gradient(g);
    if (pose->shape == EYES_SHAPE_CAPSULE) {
        build_q2_lut(g, g->body.hw < g->body.hh ? g->body.hw : g->body.hh);
    } else if (pose->shape == EYES_SHAPE_CHEVRON) {
        build_q2_lut(g, g->half_main);
    }

    int32_t du0, du1, dv0, dv1;
    float m[4];
    rotate_rect(rotation, g->x0, g->y0, g->x1, g->y1, &du0, &du1, &dv0, &dv1, m);
    clip_span(&du0, &du1, s->cap_w);
    clip_span(&dv0, &dv1, s->cap_h);
    s->ox = du0;
    s->oy = dv0;
    s->w = du1 - du0 + 1;
    s->h = dv1 - dv0 + 1;

    job->s = s;
    job->mxx = m[0];
    job->mxy = m[1];
    job->myx = m[2];
    job->myy = m[3];
    /* Row mirroring needs rows to be constant-x columns (90/270) and an x-symmetric shape. */
    job->row_mirror = g->mirror_x && m[0] == 0.0f;
    raster_shape(job);

    job->rotation = rotation;
    bool mirrored_inline = pose->shape == EYES_SHAPE_CAPSULE && m[0] == 0.0f;
    if (job->row_mirror && !mirrored_inline) {
        mirror_rows(job);
    }
    /* Only after mirroring: the highlight sits on source rows and must not be copied across. */
    if (gloss && (pose->shape == EYES_SHAPE_CAPSULE || pose->shape == EYES_SHAPE_SPIRAL ||
                  pose->shape == EYES_SHAPE_OVAL)) {
        paint_glint(g, rotation, s);
    }
}

/* Spinning spiral: everything but the stripes is independent of the phase, so per settled pose
 * the plain orb (no stripes, no gloss) and each interior pixel's spiral coordinate are built
 * once; every spin frame then only shifts that coordinate, blends the stripes and re-glosses. */
#define SPIRAL_CACHE 2
#define S0_OUTSIDE   0xFFFFu /* not interior: the base pixel as is */
#define S0_CENTER    0xFFFEu /* near the center, where stripes fade out: shaded directly */
typedef struct {
    eyes_raster_pose_t key; /* spin and stripes zeroed */
    int rotation;
    bool valid;
    uint32_t used;
    int32_t ox, oy, w, h;
    size_t cap;
    uint16_t *base;
    uint16_t *s0; /* spiral coordinate in 1/65536 arm periods */
} spiral_cache_t;
static spiral_cache_t s_spiral_cache[SPIRAL_CACHE];
static uint32_t s_spiral_clock;

static bool raster_pose_same(const eyes_raster_pose_t *a, const eyes_raster_pose_t *b)
{
    return a->shape == b->shape && a->dir == b->dir && a->w == b->w && a->h == b->h &&
           a->stroke_basis == b->stroke_basis && a->squash == b->squash && a->cy == b->cy && a->ticks == b->ticks &&
           a->accent == b->accent && a->side == b->side && a->accent_opacity == b->accent_opacity;
}

static spiral_cache_t *spiral_entry(const eyes_raster_pose_t *pose, int rotation)
{
    eyes_raster_pose_t key = *pose;
    key.spin = 0.0f;
    key.stripes = 0.0f;
    spiral_cache_t *victim = &s_spiral_cache[0];
    for (int i = 0; i < SPIRAL_CACHE; i++) {
        spiral_cache_t *e = &s_spiral_cache[i];
        if (e->valid && e->rotation == rotation && raster_pose_same(&e->key, &key)) {
            e->used = ++s_spiral_clock;
            return e;
        }
        if (!e->valid || e->used < victim->used) {
            victim = e;
        }
    }
    int32_t w, h;
    eyes_raster_bounds(&key, rotation, &w, &h);
    size_t n = (size_t)w * (size_t)h;
    if (victim->cap < n) {
        free(victim->base);
        free(victim->s0);
        victim->base = malloc(n * sizeof(uint16_t));
        victim->s0 = malloc(n * sizeof(uint16_t));
        victim->cap = (victim->base && victim->s0) ? n : 0;
        if (!victim->cap) {
            victim->valid = false;
            return NULL;
        }
    }
    eyes_sprite_t base = { victim->base, w, h, 0, 0, 0, 0 };
    render_full(&key, rotation, &base, false);
    victim->ox = base.ox;
    victim->oy = base.oy;
    victim->w = base.w;
    victim->h = base.h;

    const geom_t *g = &s_job.g; /* geometry of the render just done */
    for (int32_t v = 0; v < base.h; v++) {
        for (int32_t u = 0; u < base.w; u++) {
            float du = (float)(base.ox + u);
            float dv = (float)(base.oy + v);
            float x = s_job.mxx * du + s_job.mxy * dv;
            float y = s_job.myx * du + s_job.myy * dv;
            float qy = y - g->body.cy;
            uint16_t code = S0_OUTSIDE;
            if (sd_stadium_lo(x, qy, g->body.hw, g->body.hh) < CAP_LUT_DMIN) {
                float r = fast_sqrtf(x * x + qy * qy);
                if (r < 10.0f) {
                    code = S0_CENTER;
                } else {
                    float sc = g->spin_dir * fast_atan2f(qy, x) * (SPIRAL_ARMS / 6.28318531f) +
                               r * g->spiral_inv_pitch + 4.0f;
                    float f = sc - (float)(int32_t)sc;
                    code = (uint16_t)(f * 65535.0f);
                    code = code >= S0_CENTER ? S0_CENTER - 1u : code;
                }
            }
            victim->s0[v * base.w + u] = code;
        }
    }
    victim->key = key;
    victim->rotation = rotation;
    victim->valid = true;
    victim->used = ++s_spiral_clock;
    return victim;
}

static void render_spiral_spin(const eyes_raster_pose_t *pose, int rotation, eyes_sprite_t *s)
{
    spiral_cache_t *e = spiral_entry(pose, rotation);
    if (!e || (int32_t)s->cap_w < e->w || (int32_t)s->cap_h < e->h) {
        render_full(pose, rotation, s, true);
        return;
    }
    geom_t *g = &s_job.g;
    setup_geom(g, pose);
    s->ox = e->ox;
    s->oy = e->oy;
    s->w = e->w;
    s->h = e->h;
    int32_t du0, du1, dv0, dv1;
    float m[4];
    rotate_rect(rotation, 0, 0, 0, 0, &du0, &du1, &dv0, &dv1, m);

    /* Stripe test in 16-bit phase units: |f - 1/2| < HALF + 0.5 px (in arm periods). */
    uint16_t phase = (uint16_t)(pose->spin * 65536.0f);
    float pitch = g->spiral_pitch;
    float amt = SPIRAL_OPA * pose->stripes;
    int32_t reach = (int32_t)((SPIRAL_HALF + 0.5f / pitch) * 65536.0f);
    size_t n = (size_t)e->w * (size_t)e->h;
    for (size_t k = 0; k < n; k++) {
        uint16_t code = e->s0[k];
        uint16_t base = e->base[k];
        if (code == S0_OUTSIDE) {
            s->px[k] = base;
            continue;
        }
        float a;
        if (code == S0_CENTER) {
            float du = (float)(e->ox + (int32_t)(k % (size_t)e->w));
            float dv = (float)(e->oy + (int32_t)(k / (size_t)e->w));
            float x = m[0] * du + m[1] * dv;
            float qy = m[2] * du + m[3] * dv - g->body.cy;
            float r = fast_sqrtf(x * x + qy * qy);
            float sc = g->spin_dir * fast_atan2f(qy, x) * (SPIRAL_ARMS / 6.28318531f) + r * g->spiral_inv_pitch -
                       pose->spin + 4.0f;
            float f = sc - (float)(int32_t)sc;
            a = amt * smoothstepf(2.0f, 10.0f, r) *
                clampf(0.0f, 0.5f + (SPIRAL_HALF - fabsf(f - 0.5f)) * pitch, 1.0f);
        } else {
            int32_t dd = (int32_t)(uint16_t)(code - phase) - 32768;
            dd = dd < 0 ? -dd : dd;
            if (dd >= reach) {
                s->px[k] = base;
                continue;
            }
            a = amt * clampf(0.0f, 0.5f + (SPIRAL_HALF - (float)dd / 65536.0f) * pitch, 1.0f);
        }
        if (a <= 0.0f) {
            s->px[k] = base;
            continue;
        }
        rgb_t c = unpack(base);
        over(&c, s_spiral, a);
        s->px[k] = pack(c);
    }
    paint_glint(g, rotation, s);
}

/* The ring tables pay off only for a pose that holds still. While the orb is still morphing, the
 * pose changes every frame, so the rings are drawn straight into the sprite (they are cheap now that
 * they need no angle) and show from the first frame instead of after the morph. */
static bool spiral_pose_settled(const eyes_raster_pose_t *pose, int rotation)
{
    /* Two eyes alternate calls, so remember one pose per eye. */
    static eyes_raster_pose_t last[2];
    static int last_rotation[2] = { -1, -1 };
    static int next;
    eyes_raster_pose_t key = *pose;
    key.spin = 0.0f;
    key.stripes = 0.0f;
    for (int i = 0; i < 2; i++) {
        if (last_rotation[i] == rotation && raster_pose_same(&last[i], &key)) {
            return true;
        }
    }
    last[next] = key;
    last_rotation[next] = rotation;
    next ^= 1;
    return false;
}

typedef struct {
    seg_t lines[16];
    float widths[16];
    int nlines, ndiscs, nhearts, nverts;
    float discs[8][3], hearts[2][3], verts[10][2];
} accent_ink_t;

static void accent_line(accent_ink_t *a, float x0, float y0, float x1, float y1, float r)
{
    a->lines[a->nlines] = make_seg(x0, y0, x1, y1);
    a->widths[a->nlines++] = r;
}

static void accent_disc(accent_ink_t *a, float x, float y, float r)
{
    float *d = a->discs[a->ndiscs++];
    d[0] = x; d[1] = y; d[2] = r;
}

static void accent_plus(accent_ink_t *a, float x, float y, float r)
{
    accent_line(a, x - r, y, x + r, y, 1.2f);
    accent_line(a, x, y - r, x, y + r, 1.2f);
}

static void accent_z(accent_ink_t *a, float x, float y, float r)
{
    accent_line(a, x - r, y - r, x + r, y - r, 1.2f);
    accent_line(a, x + r, y - r, x - r, y + r, 1.2f);
    accent_line(a, x - r, y + r, x + r, y + r, 1.2f);
}

static void setup_accent_ink(accent_ink_t *a, eyes_accent_t kind)
{
    memset(a, 0, sizeof(*a));
    switch (kind) {
    case EYES_ACCENT_NONE: break;
    case EYES_ACCENT_RAYS:
        accent_line(a, -13, -2, -18, -12, 1.8f);
        accent_line(a, -2, -5, -2, -19, 1.8f);
        accent_line(a, 9, -2, 16, -14, 1.8f);
        break;
    case EYES_ACCENT_SIDE_RAYS:
        accent_line(a, -5, -7, 11, -16, 1.8f);
        accent_line(a, -2, 0, 18, 0, 1.8f);
        accent_line(a, -5, 7, 11, 16, 1.8f);
        break;
    case EYES_ACCENT_SPARKLES: {
        static const float points[8][2] = {
            {0,-14}, {3,-3}, {11,0}, {3,3}, {0,14}, {-3,3}, {-11,0}, {-3,-3}
        };
        a->nverts = 8;
        memcpy(a->verts, points, sizeof(points));
        accent_plus(a, 15, -13, 4);
        accent_plus(a, -14, 12, 3);
        break;
    }
    case EYES_ACCENT_STAR:
        a->nverts = 10;
        for (int i = 0; i < 10; i++) {
            float t = (float)i * 0.62831853f - 1.57079633f;
            float r = (i & 1) ? 6.5f : 16.0f;
            a->verts[i][0] = r * cosf(t);
            a->verts[i][1] = r * sinf(t);
        }
        break;
    case EYES_ACCENT_QUESTION: {
        static const float points[8][2] = {
            {-8,-9}, {-7,-14}, {-2,-17}, {5,-16}, {9,-11}, {7,-5}, {0,0}, {0,5}
        };
        for (int i = 1; i < 8; i++) {
            accent_line(a, points[i-1][0], points[i-1][1], points[i][0], points[i][1], 2.2f);
        }
        accent_disc(a, 0, 13, 2.8f);
        break;
    }
    case EYES_ACCENT_HEARTS:
        a->nhearts = 2;
        a->hearts[0][0] = -6; a->hearts[0][1] = 6; a->hearts[0][2] = 9;
        a->hearts[1][0] = 10; a->hearts[1][1] = -11; a->hearts[1][2] = 6;
        break;
    case EYES_ACCENT_BLUSH:
        for (int i = 0; i < 4; i++) {
            float x = -13.0f + 8.0f * (float)i;
            accent_line(a, x, 3, x + 3, -3, 1.5f);
        }
        break;
    case EYES_ACCENT_NERVOUS:
        accent_line(a, -20, -16, -20, 30, 1.6f);
        accent_line(a, 0, -38, 0, 30, 1.6f);
        accent_line(a, 20, 0, 20, 52, 1.6f);
        break;
    case EYES_ACCENT_DROP:
        a->nverts = 3;
        a->verts[0][0] = 0; a->verts[0][1] = -17;
        a->verts[1][0] = -8; a->verts[1][1] = 4;
        a->verts[2][0] = 8; a->verts[2][1] = 4;
        accent_disc(a, 0, 4, 8);
        break;
    case EYES_ACCENT_ZZZ:
        accent_z(a, -10, 11, 4);
        accent_z(a, 0, 0, 5);
        accent_z(a, 11, -13, 6);
        break;
    case EYES_ACCENT_CLOUDS:
        accent_disc(a, -9, 2, 8);
        accent_disc(a, 1, -3, 10);
        accent_disc(a, 11, 2, 8);
        accent_disc(a, 1, 5, 7);
        accent_disc(a, -12, 15, 3);
        accent_disc(a, -18, 20, 1.5f);
        break;
    }
}

static float accent_sd(const accent_ink_t *a, float x, float y)
{
    float d = 1e9f;
    for (int i = 0; i < a->nlines; i++) {
        d = fminf(d, fast_sqrtf(seg_dist2(&a->lines[i], x, y)) - a->widths[i]);
    }
    for (int i = 0; i < a->ndiscs; i++) {
        float dx = x - a->discs[i][0], dy = y - a->discs[i][1];
        d = fminf(d, fast_sqrtf(dx * dx + dy * dy) - a->discs[i][2]);
    }
    for (int i = 0; i < a->nhearts; i++) {
        float r = a->hearts[i][2], scale = r / 0.60355339f;
        float dx = x - a->hearts[i][0], dy = y - a->hearts[i][1];
        d = fminf(d, heart_sd(dx / scale, (r - dy) / scale) * scale);
    }
    if (a->nverts) {
        float pd = 1e9f;
        bool inside = false;
        for (int i = 0, j = a->nverts - 1; i < a->nverts; j = i++) {
            float ax = a->verts[i][0], ay = a->verts[i][1];
            float bx = a->verts[j][0], by = a->verts[j][1];
            seg_t edge = make_seg(ax, ay, bx, by);
            pd = fminf(pd, seg_dist2(&edge, x, y));
            if ((ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax) {
                inside = !inside;
            }
        }
        d = fminf(d, fast_sqrtf(pd) * (inside ? -1.0f : 1.0f));
    }
    return d;
}

/* Decorations are composited last, never cached into the spiral's stripe/gloss layers. */
static void paint_accent(const eyes_raster_pose_t *pose, int rotation, eyes_sprite_t *s)
{
    if (pose->accent == EYES_ACCENT_NONE || !(pose->accent_opacity > 0.0f)) return;
    eyes_raster_pose_t bare = *pose;
    bare.accent = EYES_ACCENT_NONE;
    geom_t g;
    setup_geom(&g, &bare);
    accent_box_t a;
    if (!accent_box(&g, pose, &a)) return;
    accent_ink_t ink;
    setup_accent_ink(&ink, pose->accent);
    int32_t x0, x1, y0, y1;
    float m[4];
    rotate_rect(rotation, (int32_t)floorf(a.cx - a.hw), (int32_t)floorf(a.cy - a.hh),
                (int32_t)ceilf(a.cx + a.hw), (int32_t)ceilf(a.cy + a.hh), &x0, &x1, &y0, &y1, m);
    x0 = x0 < s->ox ? s->ox : x0;
    x1 = x1 >= s->ox + s->w ? s->ox + s->w - 1 : x1;
    y0 = y0 < s->oy ? s->oy : y0;
    y1 = y1 >= s->oy + s->h ? s->oy + s->h - 1 : y1;
    float opacity = clampf(0.0f, pose->accent_opacity, 1.0f);
    for (int32_t v = y0; v <= y1; v++) {
        for (int32_t u = x0; u <= x1; u++) {
            float x = m[0] * (float)u + m[1] * (float)v;
            float y = m[2] * (float)u + m[3] * (float)v - a.cy;
            x -= a.cx;
            /* Letter-like icons remain readable on both eyes. */
            if (pose->accent != EYES_ACCENT_QUESTION && pose->accent != EYES_ACCENT_ZZZ) x *= a.side;
            float d = accent_sd(&ink, x, y);
            if (d >= 0.5f) continue;
            float t = clampf(0.0f, (y + 18.0f) / 36.0f, 1.0f);
            rgb_t color = s_orb_top;
            over(&color, s_orb_bottom, t);
            over(&color, s_orb_ring, 0.45f * cov(-d - 1.2f));
            size_t k = (size_t)(v - s->oy) * (size_t)s->w + (size_t)(u - s->ox);
            rgb_t c = unpack(s->px[k]);
            over(&c, color, opacity * cov(d));
            s->px[k] = pack(c);
        }
    }
}

void eyes_raster_render(const eyes_raster_pose_t *pose, int rotation, eyes_sprite_t *s)
{
    ensure_colors();
    if (pose->shape == EYES_SHAPE_SPIRAL && pose->stripes > 0.0f && spiral_pose_settled(pose, rotation)) {
        render_spiral_spin(pose, rotation, s);
    } else {
        render_full(pose, rotation, s, true);
    }
    paint_accent(pose, rotation, s);
}
