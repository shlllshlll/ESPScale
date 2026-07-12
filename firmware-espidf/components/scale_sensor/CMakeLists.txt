idf_component_register(
    SRCS "hx711.c" "scale_sensor.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
    REQUIRES esp_driver_gpio esp_timer freertos nvs_flash storage
)
