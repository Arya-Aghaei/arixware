#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stable IDs for consumers; keeps the generated MMAP_IMAGES_* header (and the ordering
 * quirks of including a generated file across component boundaries) private to images.c. */
typedef enum {
    IMAGE_CLOCK_BACKGROUND,
    IMAGE_TIMER_BACKGROUND,
    IMAGE_ALARM_BACKGROUND,
    /* Upright settings art; the screen applies panel rotation after composition. */
    IMAGE_SETTINGS_BACKGROUND,
    IMAGE_SETTINGS_BUTTON,
    IMAGE_SETTINGS_BUTTON_ACTIVE,
} image_id_t;

/** Opens the "images" asset partition. Call once at boot. */
void images_init(void);

/**
 * Returns a ready-to-draw image descriptor for the given image, decoding (and caching in
 * PSRAM) on first use. Backgrounds include a 40% black mask for content contrast.
 * Returns NULL if the asset is missing or failed to decode.
 */
const lv_image_dsc_t *images_get(image_id_t id);

#ifdef __cplusplus
}
#endif
