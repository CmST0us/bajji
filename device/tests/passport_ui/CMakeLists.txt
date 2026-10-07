cmake_minimum_required(VERSION 3.20)
project(passport_ui_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(DEVICE "${CMAKE_CURRENT_LIST_DIR}/../..")
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEVICE}/vendor/lvgl" lvgl)
add_executable(passport_ui_test test.cpp
    "${DEVICE}/components/ui/passport_ui.cpp"
    "${DEVICE}/components/ui/bajji_font_16.c"
    "${DEVICE}/components/ui/bajji_font_20.c")
target_compile_definitions(passport_ui_test PRIVATE BAJJI_BOARD_AI_PASSPORT=1
    PASSPORT_FIXTURE="${CMAKE_CURRENT_LIST_DIR}/fixture.jpg")
target_include_directories(passport_ui_test PRIVATE stubs
    "${DEVICE}/components/ui/include" "${DEVICE}/components/board_hal/include"
    "${DEVICE}/components/board_hal" "${DEVICE}/components/ble_link/include"
    "${DEVICE}/components/bridge_protocol/include" "${DEVICE}/components/wallpaper/include"
    "${DEVICE}/components/wifi_link/include")
target_link_libraries(passport_ui_test PRIVATE lvgl)
