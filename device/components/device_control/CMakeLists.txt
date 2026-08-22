idf_component_register(
    SRCS "device_control.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ble_link board_hal bridge_protocol wallpaper
)
