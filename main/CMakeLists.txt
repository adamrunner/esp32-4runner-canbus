idf_component_register(SRCS "4runner_canbus_main.c"
                    REQUIRES driver
                    PRIV_REQUIRES esp_timer
                    INCLUDE_DIRS "." "..")
