/* Host timing of the eye rasterizer: milliseconds per sprite for each shape, full quality and draft.
 *
 *   cmake -S tools/eyes_preview -B /tmp/arixware-eyes && cmake --build /tmp/arixware-eyes
 *   /tmp/arixware-eyes/eyes_bench
 *
 * Host numbers are only a guide; on the device the same draws take roughly 200x longer (see
 * docs/EYES_PERFORMANCE.md). Compare before and after a change to the rasterizer. */
#include "eyes_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

int main(void)
{
    static const struct {
        const char *name;
        eyes_shape_t shape;
        int w, h, dir;
    } cases[] = {
        { "capsule", EYES_SHAPE_CAPSULE, 150, 170, 0 }, { "arch", EYES_SHAPE_ARCH, 140, 100, 1 },
        { "half", EYES_SHAPE_HALF, 160, 150, 0 },       { "slope", EYES_SHAPE_SLOPE, 150, 130, 1 },
        { "heart", EYES_SHAPE_HEART, 150, 150, 0 },     { "worried", EYES_SHAPE_WORRIED, 160, 130, 0 },
        { "sleep", EYES_SHAPE_SLEEP, 150, 60, 0 },      { "oval", EYES_SHAPE_OVAL, 130, 170, 0 },
        { "egg", EYES_SHAPE_EGG, 160, 160, 1 },         { "lens", EYES_SHAPE_LENS, 150, 60, 0 },
    };
    printf("%-8s %10s %10s\n", "shape", "full ms", "draft ms");
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        double ms[2];
        for (int draft = 0; draft < 2; draft++) {
            eyes_raster_pose_t p = { .shape = cases[i].shape, .dir = (int8_t)cases[i].dir, .w = cases[i].w,
                                     .h = cases[i].h, .stroke_basis = (float)cases[i].h, .squash = 1, .side = 1,
                                     .draft = draft != 0 };
            eyes_sprite_t s = { .cap_w = 300, .cap_h = 300, .px = calloc(300 * 300, sizeof(uint16_t)) };
            const int runs = 60;
            double t0 = now_ms();
            for (int k = 0; k < runs; k++) eyes_raster_render(&p, 90, &s);
            ms[draft] = (now_ms() - t0) / runs;
            free(s.px);
        }
        printf("%-8s %10.2f %10.2f\n", cases[i].name, ms[0], ms[1]);
    }
    /* The square style: an idle eye and a tall one with a spiral pupil. */
    eyes_raster_set_style(EYES_STYLE_GLASS);
    static const struct { const char *name; eyes_glass_t g; } glass[] = {
        { "sq-idle", { .w = .83f, .h = .56f, .corner = .28f, .lid_gap = .26f, .top = .1f, .pupil = .41f, .pupil_x = .28f } },
        { "sq-tall", { .w = .71f, .h = .91f, .corner = 1.0f, .round_top = 1.0f, .lid_gap = .1f, .pupil = .3f } },
        { "sq-spiral", { .w = .83f, .h = .56f, .corner = .28f, .lid_gap = .26f, .pupil = .74f, .pupil_kind = 2 } },
    };
    for (unsigned i = 0; i < sizeof(glass) / sizeof(glass[0]); i++) {
        double ms[2];
        for (int draft = 0; draft < 2; draft++) {
            eyes_raster_pose_t p = { .shape = EYES_SHAPE_GLASS, .dir = 1, .w = (int)(170 * glass[i].g.w), .h = (int)(170 * glass[i].g.h),
                                     .stroke_basis = 170, .squash = 1, .glass = glass[i].g, .draft = draft != 0 };
            eyes_sprite_t s = { .cap_w = 300, .cap_h = 300, .px = calloc(300 * 300, sizeof(uint16_t)) };
            const int runs = 60;
            double t0 = now_ms();
            for (int k = 0; k < runs; k++) eyes_raster_render(&p, 90, &s);
            ms[draft] = (now_ms() - t0) / runs;
            free(s.px);
        }
        printf("%-9s %9.2f %10.2f\n", glass[i].name, ms[0], ms[1]);
    }
    return 0;
}
