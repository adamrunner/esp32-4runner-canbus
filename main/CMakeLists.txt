idf_component_register(SRCS "4runner_canbus_main.cpp"
                              "app_state.cpp"
                              "page_utils.cpp"
                              "settings_store.cpp"
                              "pages/diag_page.cpp"
                              "pages/fourrunner_page.cpp"
                              "pages/wheel_speed_page.cpp"
                              "pages/logging_page.cpp"
                              "pages/rtc_page.cpp"
                              "pages/rpm_page.cpp"
                    REQUIRES driver display_manager esp_timer nvs_flash sd_card can_logger rtc
                    INCLUDE_DIRS "." "pages")
