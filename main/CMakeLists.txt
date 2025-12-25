idf_component_register(SRCS "4runner_canbus_main.cpp"
                    REQUIRES driver display_manager esp_timer sd_card can_logger
                    INCLUDE_DIRS ".")
