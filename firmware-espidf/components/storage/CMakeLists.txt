idf_component_register(
    SRCS "storage.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
    REQUIRES nvs_flash esp_system
)
