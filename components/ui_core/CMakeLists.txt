idf_component_register(
    SRCS "ui_core.c" "ui_panel.c" "ui_screen_welcome.c" "ui_splash_asset.c" "ui_screen_eyes.c" "ui_screen_clock.c" "ui_screen_timer.c" "ui_screen_alarm.c" "ui_screen_quote.c" "ui_tools.c" "ui_draw.c" "ui_screen_settings.c" "ui_screen_update.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl eyes_renderer
    PRIV_REQUIRES audio ui_orient heap images fonts net nvs_flash esp_app_format esp_mmap_assets
)

# The pre-rotated GIF lives in the existing 5 MB assets partition, not the OTA app slot.
set(splash_inc_dir "${CMAKE_CURRENT_BINARY_DIR}/splash_include")
file(MAKE_DIRECTORY "${splash_inc_dir}")
spiffs_create_partition_assets(
    assets
    "${CMAKE_CURRENT_LIST_DIR}/../assets/generated"
    FLASH_IN_PROJECT
    MMAP_FILE_SUPPORT_FORMAT ".gif"
    IMPORT_INC_PATH "${splash_inc_dir}"
)
target_include_directories(${COMPONENT_LIB} PRIVATE "${splash_inc_dir}")
add_dependencies(${COMPONENT_LIB} assets_assets_bin)
