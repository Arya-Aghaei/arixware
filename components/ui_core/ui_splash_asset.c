#include "ui_splash_asset.h"
#include "ui_orient.h"
#include "esp_mmap_assets.h"
#include "mmap_generate_assets.h"

_Static_assert(UI_ROTATION_DEG == 90, "Regenerate splash_panel.gif for the panel orientation");

const lv_image_dsc_t *ui_splash_asset(void)
{
    static mmap_assets_handle_t assets;
    static lv_image_dsc_t image;
    if (!assets) {
        const mmap_assets_config_t config = {
            .partition_label = "assets",
            .max_files = MMAP_ASSETS_FILES,
            .checksum = MMAP_ASSETS_CHECKSUM,
            .flags = { .mmap_enable = 1 },
        };
        if (mmap_assets_new(&config, &assets) != ESP_OK) return NULL;
    }
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_RAW;
    image.header.w = image.header.h = 466;
    image.data = mmap_assets_get_mem(assets, MMAP_ASSETS_SPLASH_PANEL_GIF);
    int size = mmap_assets_get_size(assets, MMAP_ASSETS_SPLASH_PANEL_GIF);
    if (!image.data || size <= 0) return NULL;
    image.data_size = (uint32_t)size;
    return &image;
}
