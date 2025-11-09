idf_component_register(
    SRCS "Source/ota_manager.c" "Source/ota_signature.c"
    INCLUDE_DIRS "Include"
    REQUIRES esp_http_server mbedtls app_update bootloader_support nvs_flash esp_wifi
)
