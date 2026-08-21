cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/vendor/BMI270_BMM150_Sensor"
    "${CMAKE_CURRENT_LIST_DIR}/vendor/M5GFX"
    "${CMAKE_CURRENT_LIST_DIR}/vendor/M5IOE1"
    "${CMAKE_CURRENT_LIST_DIR}/vendor/M5PM1"
    "${CMAKE_CURRENT_LIST_DIR}/vendor/lvgl"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bajji_stopwatch)
