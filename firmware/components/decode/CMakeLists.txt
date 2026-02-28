idf_component_register(
    SRCS "sd_card.cc"
    INCLUDE_DIRS "." "../minimp3"
    REQUIRES driver fatfs vfs util esp_common sdmmc esp_driver_sdspi
)
