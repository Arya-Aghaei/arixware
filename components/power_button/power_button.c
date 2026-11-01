#include "power_button.h"
#include <stdatomic.h>
#include "iot_button.h"
#include "button_gpio.h"
#include "esp_log.h"

#define POWER_BUTTON_GPIO 0 /* BOOT button; strapping only matters at reset, safe as input afterward */

static const char *TAG = "power_button";
static void (*s_on_toggle)(bool standby);
static atomic_bool s_standby;

void power_button_set_standby(bool standby)
{
    atomic_store(&s_standby, standby);
}

static void single_click_cb(void *btn_handle, void *usr_data)
{
    (void)btn_handle;
    (void)usr_data;
    bool standby = !atomic_load(&s_standby);
    power_button_set_standby(standby);
    ESP_LOGI(TAG, "Power button: %s", standby ? "standby" : "wake");
    if (s_on_toggle) {
        s_on_toggle(standby);
    }
}

void power_button_init(void (*on_toggle)(bool standby))
{
    s_on_toggle = on_toggle;

    const button_config_t btn_cfg = { 0 };
    const button_gpio_config_t gpio_cfg = {
        .gpio_num = POWER_BUTTON_GPIO,
        .active_level = 0,
    };
    button_handle_t handle;
    if (iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "Power button init failed; standby toggle disabled");
        return;
    }
    iot_button_register_cb(handle, BUTTON_SINGLE_CLICK, NULL, single_click_cb, NULL);
}
