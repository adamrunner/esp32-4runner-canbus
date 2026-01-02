idf_component_register(
    SRCS "src/display_manager.c" "src/page.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd lvgl driver espressif__esp_lcd_touch_gt911
    PRIV_REQUIRES esp_timer
)
