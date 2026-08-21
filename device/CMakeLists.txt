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

# GCC 15 diagnoses the unreachable i+1 access in ESP-IDF 6.0's
# bt/host/nimble/nimble/host/store/config/src/ble_store_nvs.c:653-657 when MAX_BONDS=1.
idf_component_get_property(bt_component bt COMPONENT_LIB)
target_compile_options(${bt_component} PRIVATE -Wno-error=array-bounds)
