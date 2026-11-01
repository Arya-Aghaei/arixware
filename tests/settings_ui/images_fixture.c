/* Decode the production background for host rendering, without ESP flash services. */
#include "images.h"
#include "src/libs/lodepng/lodepng.h"
#include <assert.h>
#include <stdio.h>

static lv_image_dsc_t s_images[4];
static uint8_t *s_pixels[4];

const lv_image_dsc_t *images_get(image_id_t id)
{
    assert(id >= IMAGE_SETTINGS_BACKGROUND && id <= IMAGE_SETTINGS_BUTTON_ACTIVE);
    int index = id - IMAGE_SETTINGS_BACKGROUND;
    uint8_t *s_data = s_pixels[index];
    lv_image_dsc_t *image = &s_images[index];
    const char *path = id == IMAGE_SETTINGS_BACKGROUND ? SETTINGS_BACKGROUND_PATH :
                       id == IMAGE_SETTINGS_BUTTON ? SETTINGS_BUTTON_PATH : SETTINGS_BUTTON_ACTIVE_PATH;
    int channels = id == IMAGE_SETTINGS_BACKGROUND ? 3 : 4;
    if (!s_data) {
        unsigned width, height;
        FILE *file = fopen(path, "rb");
        assert(file);
        assert(fseek(file, 0, SEEK_END) == 0);
        long length = ftell(file);
        assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
        uint8_t *png = lv_malloc(length);
        assert(png && fread(png, 1, length, file) == (size_t)length);
        fclose(file);
        lv_draw_buf_t *decoded = NULL;
        unsigned err = lodepng_decode32((uint8_t **)&decoded, &width, &height, png, length);
        lv_free(png);
        assert(err == 0);
        assert(width == (channels == 3 ? 466 : 86) && height == width);
        s_data = lv_malloc(width * height * channels);
        s_pixels[index] = s_data;
        assert(s_data);
        for (unsigned i = 0; i < width * height; i++) {
            s_data[i * channels] = decoded->data[i * 4 + 2];
            s_data[i * channels + 1] = decoded->data[i * 4 + 1];
            s_data[i * channels + 2] = decoded->data[i * 4];
            if (channels == 4) {
                s_data[i * channels + 3] = decoded->data[i * 4 + 3];
            }
        }
        lv_draw_buf_destroy(decoded);
        image->header.magic = LV_IMAGE_HEADER_MAGIC;
        image->header.cf = channels == 4 ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB888;
        image->header.w = width;
        image->header.h = height;
        image->header.stride = width * channels;
        image->data_size = width * height * channels;
        image->data = s_data;
    }
    return image;
}

void images_fixture_deinit(void)
{
    for (unsigned i = 0; i < 4; i++) {
        lv_free(s_pixels[i]);
        s_pixels[i] = NULL;
    }
}
