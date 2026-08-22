idf_component_register(
    SRCS "wallpaper_service.cpp" "wallpaper_format.c"
    INCLUDE_DIRS "include"
    REQUIRES bridge_protocol esp_http_client mbedtls nvs_flash fatfs wear_levelling lwip esp_timer
)
