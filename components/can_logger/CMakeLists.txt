idf_component_register(
    SRCS "src/can_logger.c"
    INCLUDE_DIRS "include"
    REQUIRES sd_card freertos esp_timer
)
