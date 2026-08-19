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
