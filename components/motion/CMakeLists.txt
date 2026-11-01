idf_component_register(
    SRCS "motion.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES qmi8658 esp32_s3_touch_amoled_1_75 lvgl
)
