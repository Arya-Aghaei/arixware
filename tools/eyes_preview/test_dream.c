#include "eyes_dream.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SIZE 466
static uint16_t first[SIZE * SIZE], rotated[SIZE * SIZE], strip[SIZE * 22 + 2];
static uint32_t hash(const uint16_t *pixels)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < SIZE * SIZE; i++) h = (h ^ pixels[i]) * 16777619u;
    return h;
}
int main(void)
{
    uint32_t clip_hash[EYES_DREAM_COUNT];
    const int frame_counts[EYES_DREAM_COUNT] = {86, 21, 16};
    for (int variant = 0; variant < EYES_DREAM_COUNT; variant++)
    {
        eyes_dream_select((eyes_dream_variant_t)variant);
        assert(!eyes_dream_update(0, 0));
        assert(eyes_dream_update(0, 1));
        assert(!eyes_dream_update(99, 1));
        eyes_dream_compose(first, 0, 0, SIZE, SIZE, 0);
        clip_hash[variant] = hash(first);
        uint32_t previous = hash(first);
        int animated = 0;
        for (int frame = 0; frame < frame_counts[variant]; frame++)
        {
            eyes_dream_update((uint32_t)frame * 100, 1);
            memset(first, 0, sizeof(first));
            eyes_dream_compose(first, 0, 0, SIZE, SIZE, 0);
            uint32_t current = hash(first);
            animated += current != previous;
            previous = current;
            for (int rotation = 0; rotation < 360; rotation += 90)
            {
                memset(rotated, 0, sizeof(rotated));
                eyes_dream_compose(rotated, 0, 0, SIZE, SIZE, rotation);
                for (int y = 0; y < SIZE; y++)
                    for (int x = 0; x < SIZE; x++)
                    {
                        int px = x, py = y;
                        if (rotation == 90) {px = SIZE - 1 - y; py = x;}
                        if (rotation == 180) {px = SIZE - 1 - x; py = SIZE - 1 - y;}
                        if (rotation == 270) {px = y; py = SIZE - 1 - x;}
                        assert(first[y * SIZE + x] == rotated[py * SIZE + px]);
                    }
                for (int y = 0; y < SIZE; y += 22)
                {
                    int end = y + 22 < SIZE ? y + 22 : SIZE;
                    memset(strip, 0, sizeof(strip));
                    strip[0] = strip[SIZE * (end-y) + 1] = 0xbeef;
                    eyes_dream_compose(strip+1, 0, y, SIZE, end, rotation);
                    assert(strip[0] == 0xbeef && strip[SIZE * (end-y)+1] == 0xbeef);
                    assert(memcmp(strip+1, rotated+y*SIZE, SIZE*(end-y)*sizeof(uint16_t)) == 0);
                }
            }
        }
        assert(animated > frame_counts[variant] / 2);
        assert(eyes_dream_update(8600, 0.5f));
        assert(eyes_dream_update(8600, 0));
        memset(first, 0, sizeof(first));
        eyes_dream_compose(first, 0, 0, SIZE, SIZE, 0);
        for (int i = 0; i < SIZE*SIZE; i++) assert(first[i] == 0);
        eyes_dream_reset();
    }
    for (int a = 0; a < EYES_DREAM_COUNT; a++)
        for (int b = a+1; b < EYES_DREAM_COUNT; b++)
            assert(clip_hash[a] != clip_hash[b]);
    /* Switching on a live frame invalidates the cache without an opacity change. */
    eyes_dream_select(EYES_DREAM_SKY);
    assert(eyes_dream_update(0, 1));
    eyes_dream_select(EYES_DREAM_FENCE);
    assert(eyes_dream_update(0, 1));
    memset(first, 0, sizeof(first));
    eyes_dream_compose(first, 0, 0, SIZE, SIZE, 0);
    assert(hash(first) == clip_hash[EYES_DREAM_FENCE]);
    assert(!eyes_dream_update(0, 1));
    eyes_dream_reset();
    eyes_dream_select(EYES_DREAM_SKY);
    uint32_t fade_hash = 0;
    uint64_t previous_light = 0;
    int visible_steps = 0;
    for (int level = 0; level <= 256; level++)
    {
        /* Freeze the GIF: only opacity may change this image. */
        eyes_dream_update(0, level / 256.0f);
        memset(first, 0, sizeof(first));
        eyes_dream_compose(first, 0, 0, SIZE, SIZE, 0);
        uint64_t light = 0;
        for (int i = 0; i < SIZE*SIZE; i++)
        {
            uint16_t px = (uint16_t)((first[i] >> 8) | (first[i] << 8));
            light += (px >> 11) + ((px >> 5) & 63) + (px & 31);
        }
        assert(light >= previous_light);
        previous_light = light;
        uint32_t current = hash(first);
        visible_steps += current != fade_hash;
        fade_hash = current;
    }
    printf("Fade brightness steps: %d\n", visible_steps);
    assert(visible_steps > 200); /* A long fade must not collapse to 33 brightness jumps. */
    /* A partially faded cloud must blend over existing panel content as well. */
    eyes_dream_update(0, 1);
    memset(first, 0, sizeof(first));
    eyes_dream_compose(first, 0, 0, SIZE, SIZE, 0);
    const uint16_t bg = 0x1234;
    for (int i = 0; i < SIZE*SIZE; i++) rotated[i] = (uint16_t)((bg >> 8) | (bg << 8));
    eyes_dream_update(0, 0.5f);
    eyes_dream_compose(rotated, 0, 0, SIZE, SIZE, 0);
    for (int i = 0; i < SIZE*SIZE; i++)
    {
        uint16_t source = (uint16_t)((first[i] >> 8) | (first[i] << 8));
        uint16_t result = (uint16_t)((rotated[i] >> 8) | (rotated[i] << 8));
        if (!source) { assert(result == bg); continue; }
        const int shifts[] = {11, 5, 0}, masks[] = {31, 63, 31};
        for (int c = 0; c < 3; c++)
        {
            int a = (source >> shifts[c]) & masks[c];
            int b = (bg >> shifts[c]) & masks[c];
            int value = (result >> shifts[c]) & masks[c];
            assert(value >= (a < b ? a : b) && value <= (a > b ? a : b));
        }
    }
    eyes_dream_reset();
    puts("PASS: all three dream clips, stable selection, rotations, strip bounds and fading");
    return 0;
}
