if(IDF_TARGET STREQUAL "esp32c3")
    set(UI_SOURCE "passport_ui.cpp")
    set(UI_EXTRA_REQUIRES "")
else()
    set(UI_SOURCE "diagnostics_ui.cpp")
    set(UI_EXTRA_REQUIRES webp_decoder)
endif()
idf_component_register(
    SRCS ${UI_SOURCE} "bajji_font_16.c" "bajji_font_20.c" "bajji_font_24.c"
    INCLUDE_DIRS "include"
    REQUIRES ble_link board_hal ip_bridge wallpaper wifi_link lvgl ${UI_EXTRA_REQUIRES}
)
