idf_component_register(
    SRCS "ip_bridge.c" "ipv4_packet.c"
    INCLUDE_DIRS "include"
    REQUIRES ble_link bridge_protocol esp_netif lwip
)
