idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
    REQUIRES esp_wifi esp_event esp_netif nvs_flash storage
)
