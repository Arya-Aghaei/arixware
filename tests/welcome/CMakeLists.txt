cmake_minimum_required(VERSION 3.12.4)
project(welcome_test C)
set(CMAKE_C_STANDARD 11)
get_filename_component(ROOT ../.. ABSOLUTE)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(LV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_USE_STDLIB_MALLOC=1 LV_USE_GIF=1)
add_subdirectory(${ROOT}/managed_components/lvgl__lvgl lvgl_build)
# Embed the same generated GIF in a host fixture, replacing only flash mapping.
file(READ "${ROOT}/components/assets/generated/splash_panel.gif" ASSET HEX)
string(REGEX REPLACE "(..)" "0x\\1," ASSET "${ASSET}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/asset_fixture.c"
    "#include \"ui_splash_asset.h\"\nstatic const uint8_t data[]={${ASSET}};\nextern int missing_asset;\nconst lv_image_dsc_t *ui_splash_asset(void) {\nstatic const lv_image_dsc_t asset={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RAW,.w=466,.h=466},.data_size=sizeof(data),.data=data};\nreturn missing_asset ? NULL : &asset;\n}\n")
add_executable(welcome_test main.c "${CMAKE_CURRENT_BINARY_DIR}/asset_fixture.c"
    ${ROOT}/components/ui_core/ui_screen_welcome.c
    ${ROOT}/components/fonts/fredoka_bold_40.c)
target_include_directories(welcome_test PRIVATE
    ${ROOT}/components/ui_core ${ROOT}/components/ui_core/include
    ${ROOT}/components/ui_orient/include ${ROOT}/components/fonts/include
    ${ROOT}/components/net/include ${ROOT}/components/eyes_renderer/include
    ${ROOT}/tests/net/profile_stubs)
target_link_libraries(welcome_test PRIVATE lvgl m)
enable_testing()
add_test(NAME welcome COMMAND welcome_test)
