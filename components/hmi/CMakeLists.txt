idf_component_register(
    SRCS
        "hmi_main.c"
        "screens/screen_dashboard.c"
        "screens/screen_channel.c"
        "screens/screen_devices.c"
        "screens/screen_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES
        bsp
        daq
        esp_lvgl_port
        lvgl
        log
        freertos
        nvs_flash
        esp_wifi
)
