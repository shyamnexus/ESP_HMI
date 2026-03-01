idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        bsp
        hmi
        daq
        nvs_flash
        esp_event
        esp_wifi
)
