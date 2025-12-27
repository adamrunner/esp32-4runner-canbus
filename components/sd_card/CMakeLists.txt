idf_component_register(
    SRCS "src/sd_card.c"
    INCLUDE_DIRS "include"
    REQUIRES driver vfs fatfs sdmmc esp_driver_sdspi rtc
)
