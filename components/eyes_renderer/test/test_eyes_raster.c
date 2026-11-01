/* Host proof:
 * cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 *   components/eyes_renderer/eyes_raster.c components/eyes_renderer/test/test_eyes_raster.c \
 *   -lm -o .eyes-raster-test && ./.eyes-raster-test
 */
#include "../eyes_raster.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef EYES_RASTER_BASELINE
void baseline_render(const eyes_raster_pose_t *, int, eyes_sprite_t *);
void baseline_bounds(const eyes_raster_pose_t *, int, int32_t *, int32_t *);
#endif

static eyes_sprite_t render(const eyes_raster_pose_t *p, int rotation)
{
    int32_t w, h;
    eyes_raster_bounds(p, rotation, &w, &h);
    if (w <= 0 || h <= 0 || w > 256 || h > 256) {
        fprintf(stderr, "bounds %dx%d shape=%d accent=%d size=%dx%d squash=%g\n",
                w, h, p->shape, p->accent, p->w, p->h, (double)p->squash);
        abort();
    }
    eyes_sprite_t s = { .px = malloc((size_t)w * (size_t)h * sizeof(uint16_t)),
                        .cap_w = w, .cap_h = h };
    assert(s.px);
    memset(s.px, 0xA5, (size_t)w * (size_t)h * sizeof(uint16_t));
    eyes_raster_render(p, rotation, &s);
    assert(s.w == w && s.h == h);
    return s;
}

static uint16_t at(const eyes_sprite_t *s, int32_t x, int32_t y)
{
    x -= s->ox; y -= s->oy;
    if (x < 0 || x >= s->w || y < 0 || y >= s->h) return 0;
    return s->px[y * s->w + x];
}

static void logical(int rotation, int32_t u, int32_t v, int32_t *x, int32_t *y)
{
    switch (rotation) {
    case 90: *x = v; *y = -u; break;
    case 180: *x = -u; *y = -v; break;
    case 270: *x = -v; *y = u; break;
    default: *x = u; *y = v; break;
    }
}

static void accents(void)
{
    eyes_raster_pose_t p = { .shape = EYES_SHAPE_SPIRAL, .dir = 1, .w = 153, .h = 170,
        .stroke_basis = 170, .squash = 1.15f, .spin = 0.37f, .stripes = 1 };
    for (int rotation = 0; rotation < 360; rotation += 90) {
        eyes_sprite_t plain = render(&p, rotation);
        for (int kind = EYES_ACCENT_RAYS; kind <= EYES_ACCENT_CLOUDS; kind++) {
            for (int side = -1; side <= 1; side += 2) {
                p.accent = (eyes_accent_t)kind;
                p.side = (int8_t)side;
                p.accent_opacity = 0.45f;
                eyes_sprite_t a = render(&p, rotation);
                int outside = 0;
                for (int32_t v = 0; v < a.h; v++) {
                    for (int32_t u = 0; u < a.w; u++) {
                        int32_t pu = a.ox + u, pv = a.oy + v;
                        if (at(&a, pu, pv) == at(&plain, pu, pv)) continue;
                        int32_t x, y;
                        logical(rotation, pu, pv, &x, &y);
                        (void)y;
                        if (x * side > p.w / 2 || kind == EYES_ACCENT_BLUSH) outside++;
                    }
                }
                assert(outside > 8);
                free(a.px);
                /* Changing sides/opacities must not reuse a decorated spiral frame. */
                p.accent_opacity = 0;
                a = render(&p, rotation);
                assert(a.w == plain.w && a.h == plain.h && a.ox == plain.ox && a.oy == plain.oy);
                assert(memcmp(a.px, plain.px, (size_t)a.w * (size_t)a.h * sizeof(uint16_t)) == 0);
                free(a.px);
            }
        }
        free(plain.px);
        p.accent = EYES_ACCENT_NONE;
    }
}

static void reference_rotations(void)
{
    for (int shape = EYES_SHAPE_ARCH; shape <= EYES_SHAPE_OVAL; shape++) {
        if (shape > EYES_SHAPE_ARCH && shape < EYES_SHAPE_HALF) continue;
        eyes_raster_pose_t p = { .shape = (eyes_shape_t)shape, .dir = 1, .w = 120, .h = 140,
            .stroke_basis = 140, .squash = 1, .accent = EYES_ACCENT_HEARTS,
            .accent_opacity = 0.7f, .side = -1 };
        eyes_sprite_t base = render(&p, 0);
        for (int rotation = 90; rotation < 360; rotation += 90) {
            eyes_sprite_t rotated = render(&p, rotation);
            for (int32_t v = 0; v < rotated.h; v++) {
                for (int32_t u = 0; u < rotated.w; u++) {
                    int32_t x, y;
                    logical(rotation, rotated.ox + u, rotated.oy + v, &x, &y);
                    assert(rotated.px[v * rotated.w + u] == at(&base, x, y));
                }
            }
            free(rotated.px);
        }
        free(base.px);
    }
}

static void baseline(void)
{
#ifdef EYES_RASTER_BASELINE
    for (int shape = EYES_SHAPE_CAPSULE; shape <= EYES_SHAPE_SPIRAL; shape++) {
        if (shape == EYES_SHAPE_ARCH) continue; /* Deliberately changed to the chart dome. */
        for (int rotation = 0; rotation < 360; rotation += 90) {
            for (int dir = -1; dir <= 1; dir++) {
                eyes_raster_pose_t p = { .shape = (eyes_shape_t)shape, .dir = (int8_t)dir,
                    .w = 153, .h = 170, .stroke_basis = 170, .squash = 1.15f,
                    .ticks = 0.7f, .spin = 0.37f, .stripes = 1 };
                eyes_sprite_t s = render(&p, rotation);
                int32_t w, h;
                baseline_bounds(&p, rotation, &w, &h);
                assert(w == s.w && h == s.h);
                eyes_sprite_t old = { .px = malloc((size_t)w * (size_t)h * sizeof(uint16_t)),
                                     .cap_w = w, .cap_h = h };
                assert(old.px);
                baseline_render(&p, rotation, &old);
                assert(old.ox == s.ox && old.oy == s.oy && old.w == s.w && old.h == s.h);
                assert(memcmp(old.px, s.px, (size_t)w * (size_t)h * sizeof(uint16_t)) == 0);
                free(old.px); free(s.px);
            }
        }
    }
#endif
}

static int bright(uint16_t pixel)
{
    uint16_t rgb = (uint16_t)((pixel << 8) | (pixel >> 8));
    return ((rgb >> 11) & 31) >= 12;
}

static void extent(const eyes_sprite_t *s, int32_t x, int *top, int *bottom)
{
    *top = 10000;
    *bottom = -10000;
    for (int y = s->oy; y < s->oy + s->h; y++) {
        if (bright(at(s, x, y))) {
            if (y < *top) *top = y;
            *bottom = y;
        }
    }
}

static void silhouettes(void)
{
    eyes_raster_pose_t p = { .shape = EYES_SHAPE_ARCH, .dir = 1, .w = 153, .h = 136,
                            .stroke_basis = 136, .squash = 1 };
    eyes_sprite_t s = render(&p, 0);
    int top, bottom, left_top, left_bottom, right_top, right_bottom;
    extent(&s, 0, &top, &bottom);
    assert(bottom - top > 40); /* Thick dome center, not a narrow horseshoe stroke. */
    assert(bright(at(&s, 0, -25)));
    assert(!bright(at(&s, 0, 25))); /* Wide, shallow lower cutout. */
    free(s.px);
    p.shape = EYES_SHAPE_SLEEP;
    s = render(&p, 0);
    extent(&s, 0, &top, &bottom);
    assert(bottom - top >= 70); /* Resting lens height is roughly half its width. */
    free(s.px);
    p.h = 6;
    s = render(&p, 0);
    extent(&s, 0, &top, &bottom);
    assert(bottom - top <= 12); /* A blink still collapses the lens. */
    free(s.px);
    p.shape = EYES_SHAPE_WORRIED;
    p.h = 170;
    p.side = 1;
    s = render(&p, 0);
    /* Two circles on one baseline: the right one has twice the area, so 1.41x the height of the left. */
    extent(&s, -41, &left_top, &left_bottom);
    extent(&s, 26, &right_top, &right_bottom);
    int small_h = left_bottom - left_top, large_h = right_bottom - right_top;
    assert(small_h > 50 && large_h > small_h);
    assert(large_h * 100 >= small_h * 135 && large_h * 100 <= small_h * 148);
    assert(abs(right_bottom - left_bottom) <= 3); /* shared baseline */
    free(s.px);
    /* The left eye is the same shape flipped horizontally. */
    p.side = -1;
    s = render(&p, 0);
    extent(&s, 41, &left_top, &left_bottom);
    extent(&s, -26, &right_top, &right_bottom);
    assert(right_bottom - right_top == large_h && left_bottom - left_top == small_h);
    free(s.px);
}

int main(void)
{
    static const int sizes[][2] = {{153,170}, {120,140}, {52,70}, {153,12}, {12,8}, {153,6}};
    static const float squashes[] = {0.02f, 0.2f, 1.0f, 1.15f};
    unsigned cases = 0;
    for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
        for (size_t q = 0; q < sizeof(squashes) / sizeof(squashes[0]); q++) {
            for (int shape = EYES_SHAPE_CAPSULE; shape <= EYES_SHAPE_OVAL; shape++) {
                for (int kind = EYES_ACCENT_NONE; kind <= EYES_ACCENT_CLOUDS; kind++) {
                    for (int rotation = 0; rotation < 360; rotation += 90) {
                        for (int dir = -1; dir <= 1; dir++) {
                            eyes_raster_pose_t p = { .shape = (eyes_shape_t)shape, .dir = (int8_t)dir,
                                .w = sizes[z][0], .h = sizes[z][1], .stroke_basis = sizes[z][1],
                                .squash = squashes[q], .cy = 13, .ticks = 0.4f,
                                .spin = 0.37f, .stripes = 0.9f, .accent = (eyes_accent_t)kind,
                                .accent_opacity = 0.65f, .side = dir < 0 ? -1 : 1 };
                            eyes_sprite_t s = render(&p, rotation);
                            free(s.px);
                            cases++;
                        }
                    }
                }
            }
        }
    }
    accents();
    reference_rotations();
    silhouettes();
    baseline();
    printf("eyes raster: %u shape/accent/rotation cases passed\n", cases);
    return 0;
}
