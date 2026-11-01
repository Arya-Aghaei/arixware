#include "images.h"
#include "esp_mmap_assets.h"
#include "mmap_generate_images.h"
#include "qoi.h"
#include "esp_log.h"
#include <stdlib.h>
#include <stdint.h>

/* Few static backgrounds and control skins; a flat array is simpler than a hash map here. */
#define MAX_CACHED_IMAGES 6

typedef struct {
    image_id_t id;
    lv_image_dsc_t dsc;
    void *pixels;
} cached_image_t;

static const char *TAG = "images";
static mmap_assets_handle_t s_assets;
static cached_image_t s_cache[MAX_CACHED_IMAGES];
static int s_cache_count;

static int mmap_index_for(image_id_t id)
{
    switch (id) {
    case IMAGE_CLOCK_BACKGROUND:
        return MMAP_IMAGES_CLOCK_BACKGROUND_QOI;
    case IMAGE_TIMER_BACKGROUND:
        return MMAP_IMAGES_TIMER_BACKGROUND_QOI;
    case IMAGE_ALARM_BACKGROUND:
        return MMAP_IMAGES_ALARM_BACKGROUND_QOI;
    case IMAGE_SETTINGS_BACKGROUND:
        return MMAP_IMAGES_SETTINGS_BACKGROUND_QOI;
    case IMAGE_SETTINGS_BUTTON:
        return MMAP_IMAGES_SETTINGS_BUTTON_QOI;
    case IMAGE_SETTINGS_BUTTON_ACTIVE:
        return MMAP_IMAGES_SETTINGS_BUTTON_ACTIVE_QOI;
    }
    return -1;
}

void images_init(void)
{
    const mmap_assets_config_t cfg = {
        .partition_label = "images",
        .max_files = MMAP_IMAGES_FILES,
        .checksum = MMAP_IMAGES_CHECKSUM,
        .flags = { .mmap_enable = 1 },
    };
    if (mmap_assets_new(&cfg, &s_assets) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open image assets");
    }
}

const lv_image_dsc_t *images_get(image_id_t id)
{
    if (!s_assets) {
        return NULL;
    }
    for (int i = 0; i < s_cache_count; i++) {
        if (s_cache[i].id == id) {
            return &s_cache[i].dsc;
        }
    }
    if (s_cache_count >= MAX_CACHED_IMAGES) {
        ESP_LOGW(TAG, "Image cache full");
        return NULL;
    }

    int index = mmap_index_for(id);
    const uint8_t *qoi_data = index >= 0 ? mmap_assets_get_mem(s_assets, index) : NULL;
    int qoi_len = index >= 0 ? mmap_assets_get_size(s_assets, index) : 0;
    if (!qoi_data || qoi_len <= 0) {
        ESP_LOGW(TAG, "Image %d missing from assets", (int)id);
        return NULL;
    }

    /* Backgrounds are opaque; small control skins retain their transparent glow. Large
     * backgrounds use PSRAM via malloc, while small skins can stay in internal memory. */
    int channels = id == IMAGE_SETTINGS_BUTTON || id == IMAGE_SETTINGS_BUTTON_ACTIVE ? 4 : 3;
    qoi_desc desc;
    void *pixels = qoi_decode(qoi_data, qoi_len, &desc, channels);
    if (!pixels) {
        ESP_LOGW(TAG, "Image %d: QOI decode failed", (int)id);
        return NULL;
    }

    /* QOI decodes to RGB(A) byte order, but LVGL RGB888/ARGB8888 reads memory as
     * B,G,R,(A) per pixel — swap R/B in place or colors come out channel-swapped (warm
     * orange art rendering blue). */
    uint8_t *px = pixels;
    size_t px_count = (size_t)desc.width * desc.height;
    bool dim_background = id != IMAGE_SETTINGS_BUTTON && id != IMAGE_SETTINGS_BUTTON_ACTIVE;
    for (size_t i = 0; i < px_count; i++) {
        uint8_t r = px[i * channels];
        px[i * channels] = px[i * channels + 2];
        px[i * channels + 2] = r;
        /* Bake a 40% black mask into cached backgrounds, not controls or text. */
        if (dim_background) {
            for (int c = 0; c < 3; c++) {
                px[i * channels + c] = px[i * channels + c] * 3 / 5;
            }
        }
    }
    cached_image_t *c = &s_cache[s_cache_count++];
    c->id = id;
    c->pixels = pixels;
    c->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    c->dsc.header.cf = channels == 4 ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB888;
    c->dsc.header.w = desc.width;
    c->dsc.header.h = desc.height;
    c->dsc.header.stride = desc.width * channels;
    c->dsc.data_size = desc.width * desc.height * channels;
    c->dsc.data = pixels;
    return &c->dsc;
}
