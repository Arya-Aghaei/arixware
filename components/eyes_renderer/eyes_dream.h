#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { EYES_DREAM_SKY, EYES_DREAM_FENCE, EYES_DREAM_SAVING, EYES_DREAM_COUNT } eyes_dream_variant_t;
/* Select once at the start of a dream; frame updates retain that clip. */
void eyes_dream_select(eyes_dream_variant_t variant);

/* Dream GIF playback, clipped to a cloud in logical display coordinates. */
bool eyes_dream_update(uint32_t elapsed_ms, float opacity);
void eyes_dream_compose(uint16_t *dst, int x0, int y0, int x1, int y1, int rotation);
void eyes_dream_bounds(int rotation, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1);
void eyes_dream_reset(void);
