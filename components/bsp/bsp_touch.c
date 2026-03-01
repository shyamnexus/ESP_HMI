#include "bsp.h"
#include "esp_log.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_TOUCH";

extern bsp_handles_t g_bsp_handles;

esp_err_t bsp_touch_init(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "Initialising GT911 touch controller");

    /* Create an LCD I2C IO handle (abstraction used by esp_lcd_touch) */
    const esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr             = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        .scl_speed_hz         = BSP_TOUCH_I2C_FREQ_HZ,
        .control_phase_bytes  = 1,
        .lcd_cmd_bits         = 16,   /* GT911 register address is 16-bit */
        .lcd_param_bits       = 8,
        .flags.disable_control_phase = true,
    };

    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io_handle));

    /* Touch panel configuration */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max          = BSP_LCD_H_RES,
        .y_max          = BSP_LCD_V_RES,
        .rst_gpio_num   = GPIO_NUM_NC,  /* Reset handled by IO expander */
        .int_gpio_num   = BSP_TOUCH_INT_GPIO,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, &g_bsp_handles.touch));

    ESP_LOGI(TAG, "GT911 ready");
    return ESP_OK;
}
