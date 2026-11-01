idf_component_register(
    SRCS "images.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_mmap_assets lvgl
    PRIV_REQUIRES qoi
)

# Stage only assets used by existing screens; the full PNG collection exceeds
# the image partition, and the asset packer does not recurse into subdirectories.
set(backgrounds_dir "${CMAKE_CURRENT_LIST_DIR}/../assets/backgrounds")
set(image_assets_dir "${CMAKE_CURRENT_BINARY_DIR}/image_assets")
file(MAKE_DIRECTORY "${image_assets_dir}")
configure_file("${backgrounds_dir}/png/01_clock_sunrise_466.png"
    "${image_assets_dir}/clock_background.png" COPYONLY)
configure_file("${backgrounds_dir}/png/02_settings_planet_466.png"
    "${image_assets_dir}/settings_background.png" COPYONLY)
configure_file("${backgrounds_dir}/png/04_timer_meteors_466.png"
    "${image_assets_dir}/timer_background.png" COPYONLY)
configure_file("${backgrounds_dir}/png/05_alert_asteroids_466.png"
    "${image_assets_dir}/alarm_background.png" COPYONLY)
foreach(skin settings_button settings_button_active)
    configure_file("${backgrounds_dir}/${skin}.png"
        "${image_assets_dir}/${skin}.png" COPYONLY)
endforeach()

spiffs_create_partition_assets(
    images
    "${image_assets_dir}"
    FLASH_IN_PROJECT
    MMAP_FILE_SUPPORT_FORMAT ".png"
    MMAP_SUPPORT_QOI
    IMPORT_INC_PATH "${CMAKE_CURRENT_LIST_DIR}/include"
)

# images.c includes the generated mmap_generate_images.h; make sure it exists first
# (the custom command above being marked ALL doesn't by itself order it before this
# component's own compile step).
add_dependencies(${COMPONENT_LIB} assets_images_bin)
