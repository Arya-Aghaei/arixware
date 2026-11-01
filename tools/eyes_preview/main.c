#include "lvgl.h"
#include "eyes_renderer.h"
#include "eyes_dream.h"
#include "panel_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define W 466
#define H 466

/* LVGL still needs a display for timers/screens; the eyes themselves bypass it and land in
 * panel_host's framebuffer, which is what gets dumped. */
static uint8_t s_buf[W * H * 3];

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

static bool dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("fopen");
        return false;
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    const uint16_t *fb = panel_host_fb();
    for (int i = 0; i < W * H; i++) {
        uint16_t v = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
        uint8_t r = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
        uint8_t g = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
        uint8_t b = (uint8_t)((v & 0x1F) * 255 / 31);
        fputc(r, f);
        fputc(g, f);
        fputc(b, f);
    }
    if (ferror(f)) {
        perror("write PPM");
        fclose(f);
        return false;
    }
    if (fclose(f) != 0) {
        perror("close PPM");
        return false;
    }
    printf("Wrote %s\n", path);
    return true;
}

int main(int argc, char **argv)
{
    /* Two modes:
     *   ./eyes_preview [expression]                      -> single settled frame, /tmp/eyes_preview.ppm
     *   ./eyes_preview <from> <to> <frames> [duration_ms] -> transition sequence,
     *                                                        /tmp/eyes_transition_NN.ppm
     *   ./eyes_preview --dream <duration_ms>              -> sheep dream snapshot
     *   ./eyes_preview --idle <duration_ms>               -> autonomous idle snapshot
     *   ./eyes_preview --all <existing-directory>         -> all reference expressions */
    bool saving_mode = argc == 3 && strcmp(argv[1], "--dream-saving") == 0;
    bool fence_mode = argc == 3 && strcmp(argv[1], "--dream-fence") == 0;
    bool dream_mode = saving_mode || fence_mode || (argc == 3 && strcmp(argv[1], "--dream") == 0);
    bool idle_mode = argc == 3 && strcmp(argv[1], "--idle") == 0;
    bool all_mode = argc == 3 && strcmp(argv[1], "--all") == 0;
    bool sequence_mode = !all_mode && argc >= 4;
    int expression = (!dream_mode && !idle_mode && !sequence_mode && argc > 1) ? atoi(argv[1]) : -1; /* -1 = default (Neutral) */

    lv_init();

    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(disp, s_buf, NULL, sizeof(s_buf), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_screen_load(scr);

    const eyes_renderer_api_t *api = eyes_renderer_get_default();
    api->create(scr);
    if (dream_mode) {
        extern void eyes_preview_start_dream(void);
        eyes_preview_start_dream();
        eyes_dream_select(saving_mode ? EYES_DREAM_SAVING : fence_mode ? EYES_DREAM_FENCE : EYES_DREAM_SKY);
    }

    if (all_mode) {
        for (int i = 0; i < EYES_EXPRESSION_COUNT; i++) {
            if (i > 0) {
                api->destroy();
                api->create(scr);
            }
            api->set_expression((uint8_t)i);
            for (int frame = 0; frame < 60; frame++) {
                lv_tick_inc(16);
                lv_timer_handler();
            }
            char path[1024];
            int n = snprintf(path, sizeof(path), "%s/eyes_%02d.ppm", argv[2], i);
            if (n < 0 || (size_t)n >= sizeof(path) || !dump_ppm(path)) {
                fprintf(stderr, "Cannot export expression %d\n", i);
                api->destroy();
                return 1;
            }
        }
        api->destroy();
        return 0;
    }

    if (sequence_mode) {
        int from_expr = atoi(argv[1]);
        int to_expr = atoi(argv[2]);
        int num_frames = atoi(argv[3]);
        int total_ms = (argc >= 5) ? atoi(argv[4]) : 400;

        if (from_expr >= 0 && api->set_expression) {
            api->set_expression((uint8_t)from_expr);
        }
        /* Settle at the starting expression before the transition begins. */
        for (int i = 0; i < 60; i++) {
            lv_tick_inc(16);
            lv_timer_handler();
        }

        if (api->set_expression) {
            api->set_expression((uint8_t)to_expr);
        }
        int elapsed_ms = 0;
        for (int f = 0; f < num_frames; f++) {
            int target_ms = (num_frames > 1) ? (f * total_ms / (num_frames - 1)) : 0;
            while (elapsed_ms < target_ms) {
                lv_tick_inc(16);
                lv_timer_handler();
                elapsed_ms += 16;
            }
            char path[64];
            snprintf(path, sizeof(path), "/tmp/eyes_transition_%02d.ppm", f);
            dump_ppm(path);
        }
        return 0;
    }

    if (expression >= 0 && api->set_expression) {
        api->set_expression((uint8_t)expression);
    }

    /* Advance enough virtual time for any blink-in/settle animation to finish, then
     * stop right after a render so we don't catch a mid-blink frame. */
    int preview_ms = (dream_mode || idle_mode) ? atoi(argv[2]) : 960;
    for (int elapsed = 0; elapsed < preview_ms; elapsed += 16) {
        lv_tick_inc(16);
        lv_timer_handler();
    }

    long non_zero = 0;
    const uint16_t *fb = panel_host_fb();
    for (int i = 0; i < W * H; i++) {
        if (fb[i] != 0) {
            non_zero++;
        }
    }
    fprintf(stderr, "non-black pixels: %ld / %d\n", non_zero, W * H);

    dump_ppm("/tmp/eyes_preview.ppm");
    return 0;
}
