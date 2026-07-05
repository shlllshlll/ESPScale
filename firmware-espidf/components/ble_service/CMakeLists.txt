idf_component_register(
    SRCS "ble_service.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
    REQUIRES
        bt
        nvs_flash
        storage
        protocol
        wifi_manager
        scale_sensor
)
