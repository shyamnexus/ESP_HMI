idf_component_register(
    SRCS
        "bsp_io_expander.c"
        "bsp_lcd.c"
        "bsp_touch.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_lcd
        esp_lcd_touch
        esp_lcd_touch_gt911
        log
    # CH422G IO expander is driven via raw I2C (driver/i2c_master).
    # No extra component needed — see bsp_io_expander.c.
)
