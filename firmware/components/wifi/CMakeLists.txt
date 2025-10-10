idf_component_register(
    SRCS "wifi_object.cc"
    INCLUDE_DIRS "include"
    REQUIRES util esp_http_server
    PRIV_REQUIRES esp_event esp_wifi 
)
