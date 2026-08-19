idf_component_register(
    SRCS "ble_link.c"
    INCLUDE_DIRS "include"
    REQUIRES bridge_protocol bt nvs_flash
)
