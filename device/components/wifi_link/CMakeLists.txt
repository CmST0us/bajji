idf_component_register(
    SRCS "wifi_link.c" "wifi_provision.c" "wifi_portal_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_http_server esp_hw_support esp_netif esp_timer esp_wifi ip_bridge lwip nvs_flash
)
