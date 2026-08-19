idf_component_register(
    SRCS "wallpaper_service.cpp" "wallpaper_format.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client mbedtls espressif__cjson spiffs lwip
)
