idf_component_register(
    SRCS "expressive_eyes.c" "eyes_raster.c" "eyes_theme.c" "eyes_dream.c" "eyes_panel_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
    PRIV_REQUIRES ui_orient esp_hw_support heap audio esp_lvgl_adapter log
)
