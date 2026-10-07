if(IDF_TARGET STREQUAL "esp32c3")
    set(BSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../vendor/ai-passport/components/bsp")
    idf_component_register(
        SRCS "board_hal_passport.cpp"
             "${BSP_ROOT}/src/bsp_display.c"
             "${BSP_ROOT}/src/bsp_i2c.c"
             "${BSP_ROOT}/src/bsp_audio.c"
             "${BSP_ROOT}/src/bsp_es8311_sleep_check.c"
             "${BSP_ROOT}/src/bsp_battery.c"
        INCLUDE_DIRS "include" "." "${BSP_ROOT}/include"
        REQUIRES esp_driver_gpio esp_driver_spi esp_driver_ledc esp_driver_i2c
                 esp_driver_i2s esp_lcd esp_adc esp_timer espressif__esp_codec_dev lvgl nvs_flash)
else()
idf_component_register(
    SRCS "board_hal.cpp"
    INCLUDE_DIRS "include" "."
    REQUIRES
        BMI270_BMM150_Sensor
        M5GFX
        M5IOE1
        M5PM1
        esp_driver_gpio
        esp_driver_i2s
        esp_timer
        espressif__esp_codec_dev
        espressif__i2c_bus
        lvgl
        nvs_flash
)
endif()
