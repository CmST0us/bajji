idf_component_register(
    SRCS "diagnostics_ui.cpp" "bajji_font_16.c"
    INCLUDE_DIRS "include"
    REQUIRES ble_link board_hal ip_bridge wallpaper lvgl webp_decoder
)
