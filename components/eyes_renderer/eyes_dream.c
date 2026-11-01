#include "eyes_dream.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#include "dream_frames.inc"
#include "dream_fence_frames.inc"
#include "dream_saving_frames.inc"

typedef struct {
    const uint32_t *offsets;
    const uint8_t *rle;
    unsigned frames;
} dream_clip_t;
static const dream_clip_t s_clips[EYES_DREAM_COUNT] = {
    {dream_offsets, dream_rle, DREAM_FRAME_COUNT},
    {dream_fence_offsets, dream_fence_rle, DREAM_FENCE_FRAME_COUNT},
    {dream_saving_offsets, dream_saving_rle, DREAM_SAVING_FRAME_COUNT},
};
_Static_assert(DREAM_WIDTH == DREAM_FENCE_WIDTH && DREAM_HEIGHT == DREAM_FENCE_HEIGHT &&
               DREAM_FRAME_MS == DREAM_FENCE_FRAME_MS &&
               DREAM_WIDTH == DREAM_SAVING_WIDTH && DREAM_HEIGHT == DREAM_SAVING_HEIGHT &&
               DREAM_FRAME_MS == DREAM_SAVING_FRAME_MS, "Dream clips share a playback canvas");
static eyes_dream_variant_t s_variant;


#define DISPLAY_SIZE 466
#define DREAM_X 138
#define DREAM_Y 38
static uint8_t *s_pixels;
static int s_frame = -1;
static unsigned s_alpha;
/* Fine alpha plus fixed spatial dithering avoids visible RGB565 brightness steps.
 * Build once per opacity change, rather than multiplying every cloud pixel. */
static uint16_t s_faded_palette[16][256];
static const uint8_t s_bayer[16] = {0,8,2,10, 12,4,14,6, 3,11,1,9, 15,7,13,5};

static void prepare_fade_palette(unsigned alpha)
{
    for (unsigned rank = 0; rank < 16; rank++)
    {
        unsigned rounding = rank * 16 + 8;
        for (unsigned index = 0; index < 256; index++)
        {
            unsigned r = (((index >> 5) * 31 / 7) * alpha + rounding) >> 8;
            unsigned g = ((((index >> 2) & 7) * 63 / 7) * alpha + rounding) >> 8;
            unsigned b = (((index & 3) * 31 / 3) * alpha + rounding) >> 8;
            uint16_t pixel = (uint16_t)((r << 11) | (g << 5) | b);
            s_faded_palette[rank][index] = (uint16_t)((pixel >> 8) | (pixel << 8));
        }
    }
}

void eyes_dream_select(eyes_dream_variant_t variant)
{
    if ((unsigned)variant >= EYES_DREAM_COUNT || s_variant == variant) return;
    s_variant = variant;
    s_frame = -1; /* Invalidate even if the next clip starts on the same frame number. */
}

void eyes_dream_reset(void)
{
    if (s_pixels) heap_caps_free(s_pixels);
    s_pixels = NULL;
    s_frame = -1;
    s_alpha = 0;
}

bool eyes_dream_update(uint32_t elapsed_ms, float opacity)
{
    unsigned alpha = (unsigned)lroundf(fminf(1, fmaxf(0, opacity)) * 256);
    unsigned old_alpha = s_alpha;
    if (!alpha)
    {
        if (s_pixels) eyes_dream_reset();
        return old_alpha != 0;
    }
    if (!s_pixels)
    {
        s_pixels = heap_caps_malloc(DREAM_WIDTH * DREAM_HEIGHT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pixels) return false; /* Preserve the eyes if playback memory is unavailable. */
    }
    const dream_clip_t *clip = &s_clips[s_variant];
    int frame = (elapsed_ms / DREAM_FRAME_MS) % clip->frames;
    bool changed = frame != s_frame || alpha != old_alpha;
    if (frame != s_frame)
    {
        size_t pos = 0;
        for (uint32_t i = clip->offsets[frame]; i < clip->offsets[frame + 1]; i += 2)
        {
            size_t count = clip->rle[i];
            if (!count || pos + count > DREAM_WIDTH * DREAM_HEIGHT)
            {
                eyes_dream_reset();
                return old_alpha != 0;
            }
            memset(s_pixels + pos, clip->rle[i + 1], count);
            pos += count;
        }
        if (pos != DREAM_WIDTH * DREAM_HEIGHT)
        {
            eyes_dream_reset();
            return old_alpha != 0;
        }
        s_frame = frame;
    }
    if (alpha != old_alpha) prepare_fade_palette(alpha);
    s_alpha = alpha;
    return changed;
}

void eyes_dream_bounds(int rotation, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    switch (rotation)
    {
    case 90:
        *x0 = DISPLAY_SIZE - DREAM_Y - DREAM_HEIGHT; *y0 = DREAM_X;
        *x1 = DISPLAY_SIZE - DREAM_Y; *y1 = DREAM_X + DREAM_WIDTH; break;
    case 180:
        *x0 = DISPLAY_SIZE - DREAM_X - DREAM_WIDTH; *y0 = DISPLAY_SIZE - DREAM_Y - DREAM_HEIGHT;
        *x1 = DISPLAY_SIZE - DREAM_X; *y1 = DISPLAY_SIZE - DREAM_Y; break;
    case 270:
        *x0 = DREAM_Y; *y0 = DISPLAY_SIZE - DREAM_X - DREAM_WIDTH;
        *x1 = DREAM_Y + DREAM_HEIGHT; *y1 = DISPLAY_SIZE - DREAM_X; break;
    default:
        *x0 = DREAM_X; *y0 = DREAM_Y;
        *x1 = DREAM_X + DREAM_WIDTH; *y1 = DREAM_Y + DREAM_HEIGHT; break;
    }
}

void eyes_dream_compose(uint16_t *dst, int x0, int y0, int x1, int y1, int rotation)
{
    if (!s_pixels || !s_alpha) return;
    int32_t bx0, by0, bx1, by1;
    eyes_dream_bounds(rotation, &bx0, &by0, &bx1, &by1);
    int start_x = x0 > bx0 ? x0 : bx0, end_x = x1 < bx1 ? x1 : bx1;
    int start_y = y0 > by0 ? y0 : by0, end_y = y1 < by1 ? y1 : by1;
    for (int y = start_y; y < end_y; y++)
    {
        int lx = start_x, ly = y, dx = 1, dy = 0;
        switch (rotation)
        {
        case 90: lx = y; ly = DISPLAY_SIZE - 1 - start_x; dx = 0; dy = -1; break;
        case 180: lx = DISPLAY_SIZE - 1 - start_x; ly = DISPLAY_SIZE - 1 - y; dx = -1; break;
        case 270: lx = DISPLAY_SIZE - 1 - y; ly = start_x; dx = 0; dy = 1; break;
        }
        for (int x = start_x; x < end_x; x++, lx += dx, ly += dy)
        {
            uint8_t index = s_pixels[(ly - DREAM_Y) * DREAM_WIDTH + lx - DREAM_X];
            if (!index) continue;
            unsigned rank = s_bayer[((ly & 3) << 2) | (lx & 3)];
            uint16_t pixel = s_faded_palette[rank][index];
            uint16_t *out = &dst[(y - y0) * (x1 - x0) + x - x0];
            if (*out && s_alpha != 256)
            {
                /* Preserve source-over blending if a moving eye crosses the cloud. */
                uint16_t bg = (uint16_t)((*out >> 8) | (*out << 8));
                unsigned inv = 256 - s_alpha;
                unsigned rounding = rank * 16 + 8;
                unsigned r = (((index >> 5) * 31 / 7) * s_alpha + (bg >> 11) * inv + rounding) >> 8;
                unsigned g = ((((index >> 2) & 7) * 63 / 7) * s_alpha + ((bg >> 5) & 63) * inv + rounding) >> 8;
                unsigned b = (((index & 3) * 31 / 3) * s_alpha + (bg & 31) * inv + rounding) >> 8;
                uint16_t blended = (uint16_t)((r << 11) | (g << 5) | b);
                pixel = (uint16_t)((blended >> 8) | (blended << 8));
            }
            *out = pixel;
        }
    }
}
