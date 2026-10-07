cmake_minimum_required(VERSION 3.16)

set(BAJJI_BOARD "stopwatch" CACHE STRING "Target board: stopwatch or ai_passport")
if(BAJJI_BOARD STREQUAL "ai_passport")
    set(IDF_TARGET esp32c3)
    set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.ai_passport")
    set(SDKCONFIG_DEFAULTS "sdkconfig.ai_passport.defaults")
    add_compile_definitions(BAJJI_BOARD_AI_PASSPORT=1)
elseif(NOT BAJJI_BOARD STREQUAL "stopwatch")
    message(FATAL_ERROR "Unknown BAJJI_BOARD: ${BAJJI_BOARD}")
endif()
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/vendor/lvgl")
if(BAJJI_BOARD STREQUAL "stopwatch")
    list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/vendor/BMI270_BMM150_Sensor"
        "${CMAKE_CURRENT_LIST_DIR}/vendor/M5GFX"
        "${CMAKE_CURRENT_LIST_DIR}/vendor/M5IOE1"
        "${CMAKE_CURRENT_LIST_DIR}/vendor/M5PM1")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(BAJJI_BOARD STREQUAL "ai_passport")
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.ai_passport.lock")
endif()
project(bajji_${BAJJI_BOARD})

# GCC 15 diagnoses the unreachable i+1 access in ESP-IDF 6.0's
# bt/host/nimble/nimble/host/store/config/src/ble_store_nvs.c:653-657 when MAX_BONDS=1.
idf_component_get_property(bt_component bt COMPONENT_LIB)
target_compile_options(${bt_component} PRIVATE -Wno-error=array-bounds)
