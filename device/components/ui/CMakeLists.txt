idf_component_register(
    SRCS "diagnostics_ui.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ble_link board_hal ip_bridge wallpaper lvgl
)
