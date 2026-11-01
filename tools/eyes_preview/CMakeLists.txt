cmake_minimum_required(VERSION 3.12.4)
project(eyes_preview C)

set(CMAKE_C_STANDARD 11)
set(LVGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../managed_components/lvgl__lvgl)

set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
add_compile_definitions(LV_COLOR_DEPTH=16)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(LV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(${LVGL_DIR} lvgl_build)

add_executable(eyes_preview
    main.c
    panel_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/eyes_renderer/expressive_eyes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/eyes_renderer/eyes_raster.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/eyes_renderer/eyes_dream.c
)

target_include_directories(eyes_preview PRIVATE
    stubs
    .
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/eyes_renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/eyes_renderer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/ui_orient/include
)

target_compile_definitions(eyes_preview PRIVATE EYES_PREVIEW_NO_ROTATE=1)

target_link_libraries(eyes_preview PRIVATE lvgl m)

# Scheduler regression checks use the same host panel and production renderer.
add_executable(eyes_idle_test test_idle.c panel_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/eyes_renderer/eyes_raster.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/eyes_renderer/eyes_dream.c)
get_target_property(EYES_HOST_INCLUDES eyes_preview INCLUDE_DIRECTORIES)
target_include_directories(eyes_idle_test PRIVATE ${EYES_HOST_INCLUDES})
target_compile_definitions(eyes_idle_test PRIVATE EYES_PREVIEW_NO_ROTATE=1 EYES_SLEEP_DREAM_TEST=0)
target_link_libraries(eyes_idle_test PRIVATE lvgl m)
enable_testing()
add_test(NAME eyes_idle COMMAND eyes_idle_test)

add_executable(eyes_dream_test test_dream.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/eyes_renderer/eyes_dream.c)
target_include_directories(eyes_dream_test PRIVATE ${EYES_HOST_INCLUDES})
target_link_libraries(eyes_dream_test PRIVATE m)
add_test(NAME eyes_dream COMMAND eyes_dream_test)
