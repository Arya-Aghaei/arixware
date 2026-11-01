cmake_minimum_required(VERSION 3.12.4)
project(settings_ui_test C)
set(CMAKE_C_STANDARD 11)
get_filename_component(ROOT ../.. ABSOLUTE)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(LV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_USE_STDLIB_MALLOC=1 LV_USE_LODEPNG=1 LV_USE_QRCODE=1)
add_subdirectory(${ROOT}/managed_components/lvgl__lvgl lvgl_build)
add_executable(settings_ui_test
    main.c
    images_fixture.c
    panel_stub.c
    ${ROOT}/components/ui_core/ui_core.c
    ${ROOT}/components/ui_core/ui_panel.c
    ${ROOT}/components/ui_core/ui_screen_settings.c
    ${ROOT}/components/fonts/fredoka_medium_24.c
    ${ROOT}/components/fonts/fredoka_medium_22.c
    ${ROOT}/components/fonts/fredoka_medium_28.c
    ${ROOT}/components/fonts/fredoka_medium_36.c
    ${ROOT}/components/fonts/fredoka_medium_16.c
    ${ROOT}/components/fonts/fredoka_medium_26.c
)
target_include_directories(settings_ui_test PRIVATE
    stubs
    ${ROOT}/components/audio/include
    ${ROOT}/components/ui_core/include
    ${ROOT}/components/ui_orient/include
    ${ROOT}/components/images/include
    ${ROOT}/components/fonts/include
    ${ROOT}/components/net/include
    ${ROOT}/components/eyes_renderer/include
)
target_compile_definitions(settings_ui_test PRIVATE
    UI_SETTINGS_TEST_HOOKS=1
    SETTINGS_BACKGROUND_PATH="${ROOT}/components/assets/backgrounds/png/02_settings_planet_466.png"
    SETTINGS_BUTTON_PATH="${ROOT}/components/assets/backgrounds/settings_button.png"
    SETTINGS_BUTTON_ACTIVE_PATH="${ROOT}/components/assets/backgrounds/settings_button_active.png"
)
target_link_libraries(settings_ui_test PRIVATE lvgl m)
enable_testing()
add_test(NAME settings_ui COMMAND settings_ui_test)

add_executable(settings_controller_test controller.c ${ROOT}/main/settings.c
    ${ROOT}/components/power_button/power_button.c)
target_include_directories(settings_controller_test PRIVATE
    stubs
    ${ROOT}/components/eyes_renderer/include
    ${ROOT}/main
    ${ROOT}/components/ui_core/include
    ${ROOT}/components/audio/include
    ${ROOT}/components/power_button/include
    ${ROOT}/components/net/include
)
add_test(NAME settings_controller COMMAND settings_controller_test)

add_executable(tools_test tools_test.c ${ROOT}/components/ui_core/ui_tools.c)
target_include_directories(tools_test PRIVATE
    stubs
    ${ROOT}/components/audio/include
    ${ROOT}/components/ui_core/include
)
target_link_libraries(tools_test PRIVATE lvgl m)
add_test(NAME tools COMMAND tools_test)
