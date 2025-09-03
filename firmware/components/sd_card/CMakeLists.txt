idf_component_register(
    SRCS "sd_card.cc"
    INCLUDE_DIRS "include"
    REQUIRES driver fatfs vfs util esp_common
)
