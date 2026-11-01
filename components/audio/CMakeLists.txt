idf_component_register(
    SRCS "audio.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_mmap_assets
    PRIV_REQUIRES esp32_s3_touch_amoled_1_75
)

# Track membership as well as file timestamps so deleting a WAV repacks the bank.
file(GLOB sound_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../assets/wav/*.wav")
set(sound_manifest "${CMAKE_CURRENT_BINARY_DIR}/sound_assets.txt")
file(GENERATE OUTPUT "${sound_manifest}" CONTENT "${sound_assets}\n")

spiffs_create_partition_assets(
    sounds
    "${CMAKE_CURRENT_LIST_DIR}/../assets/wav"
    DEPENDS "${sound_manifest}"
    FLASH_IN_PROJECT
    MMAP_FILE_SUPPORT_FORMAT ".wav"
    IMPORT_INC_PATH "${CMAKE_CURRENT_LIST_DIR}/include"
)

# The generated header must exist before audio and its dependent components compile.
add_dependencies(${COMPONENT_LIB} assets_sounds_bin)
