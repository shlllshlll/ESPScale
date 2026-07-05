idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        scale_sensor
        ble_service
        wifi_manager
        mqtt_handler
        http_client
        storage
        protocol
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        driver
        esp_timer
)
