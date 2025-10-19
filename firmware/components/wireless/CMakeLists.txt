idf_component_register(
    SRCS "bluetooth_object.cc" "wifi_object.cc"
    INCLUDE_DIRS "."
    REQUIRES util bt nvs_flash esp_wifi esp_http_server
)