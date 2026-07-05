idf_component_register(
    SRCS "hx711.c" "scale_sensor.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_timer freertos nvs_flash storage
)
