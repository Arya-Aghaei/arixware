#include "nvs_flash.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "ui_core.h"
#include "net_wifi.h"
#include "net_http.h"
#include "net_ota.h"
#include "motion.h"
#include "audio.h"
#include "mmap_generate_sounds.h"
#include "power_button.h"
#include "images.h"
#include "settings.h"

static const char *TAG = "main";

static void on_power_button(bool standby)
{
    bsp_display_lock(-1);
    settings_set_standby(standby);
    bsp_display_unlock();
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting display");
    if (bsp_display_start() == NULL) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    /* MADCTL rotation via bsp_display_rotation_set() misaligns this round panel's active window
     * against its physically-bonded circular mask, exposing slivers of the square substrate at the
     * edges. Rotation is done in LVGL instead (see ui_orient.h), at native panel orientation. */
    bsp_display_backlight_on();

    bsp_display_lock(-1);
    ui_core_init();
    ui_core_show_welcome();
    motion_init();
    motion_set_pickup_cb(ui_core_notify_pickup);
    motion_set_shake_cb(ui_core_notify_shake);
    bsp_display_unlock();

    images_init();
    audio_init();
    bsp_display_lock(-1);
    settings_init();
    bsp_display_unlock();
    audio_play_index(MMAP_SOUNDS_POWER_ON_WAV);
    power_button_init(on_power_button);

    /* Non-blocking: Wi-Fi connect time must never delay the boot animation. */
    net_wifi_init();
    net_http_start();
    net_ota_init();
}
