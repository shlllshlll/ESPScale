idf_component_register(
    SRCS "http_client.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
    REQUIRES esp_http_client storage protocol
)
