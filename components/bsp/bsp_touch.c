#include "bsp.h"
#include "esp_log.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_TOUCH";

extern bsp_handles_t g_bsp_handles;

esp_err_t bsp_touch_init(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "Initialising GT911 touch controller");

    /*
     * The GT911 I2C address is determined by the INT pin state when RST is
     * released:  INT low → 0x5D,  INT high → 0x14.
     * On the Waveshare ESP32-S3-Touch-LCD-4.3B the INT line has a board
     * pull-up, so the GT911 typically boots at 0x14.  Probe both addresses
     * and use whichever one the chip actually responds to.
     */
    static const uint8_t candidates[] = {
        ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,        /* 0x5D – INT low  at reset */
        ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, /* 0x14 – INT high at reset */
    };

    uint8_t gt911_addr = 0;
    for (int i = 0; i < 2; i++) {
        if (i2c_master_probe(bus, candidates[i], pdMS_TO_TICKS(20)) == ESP_OK) {
            gt911_addr = candidates[i];
            ESP_LOGI(TAG, "GT911 found at I2C address 0x%02X", gt911_addr);
            break;
        }
        ESP_LOGW(TAG, "GT911 not at 0x%02X", candidates[i]);
    }

    if (gt911_addr == 0) {
        ESP_LOGE(TAG, "GT911 not found at any known I2C address (0x%02X or 0x%02X)",
                 ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
                 ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);
        return ESP_ERR_NOT_FOUND;
    }

    /* Create the LCD I2C IO handle at the detected address */
    const esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr             = gt911_addr,
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
