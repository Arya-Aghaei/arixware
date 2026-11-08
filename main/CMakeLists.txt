idf_component_register(
    SRCS "main.c" "settings.c"
    INCLUDE_DIRS "."
    REQUIRES ui_core eyes_renderer esp32_s3_touch_amoled_1_75 nvs_flash net motion audio power_button images
)
