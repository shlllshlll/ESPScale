idf_component_register(
    SRCS "mqtt_handler.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
    REQUIRES mqtt storage protocol
)
