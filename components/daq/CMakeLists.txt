idf_component_register(
    SRCS
        "daq_manager.c"
        "daq_uart.c"
        "daq_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_wifi
        esp_netif
        mqtt
        nvs_flash
        json
        log
        freertos
)
