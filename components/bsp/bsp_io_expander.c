/**
 * @file bsp_io_expander.c
 * @brief Raw I2C driver for the CH422G IO expander on the
 *        Waveshare ESP32-S3-Touch-LCD-4.3B.
 *
 * The CH422G is NOT a standard register-based I2C device.  It uses
 * separate 7-bit I2C addresses for different internal registers:
 *
 *   0x24  –  MODE register  (write 0x01 → enable GPIO output on pins 0-7)
 *   0x38  –  OUTPUT register (write bit-mask → drive GPIO pins)
 *   0x26  –  INPUT register  (read  bit-mask ← sample GPIO pins)
 *
 * On the 4.3B board the relevant output bits are (confirmed from schematic):
 *   bit 1 (IO1)  –  CTP_RST  (GT911 touch-reset, active-low)
 *   bit 2 (IO2)  –  BL_CTRL  (LCD backlight enable, active-high)
 *   bit 3 (IO3)  –  LCD_RST  (RGB panel reset, not used in direct-drive mode)
 *
 * Sequence to enable all peripherals:
 *   1. Write 0x01 to 0x24  → GPIO output mode
 *   2. Write 0x00 to 0x38  → all outputs low  (touch in reset, BL off)
 *   3. Wait 10 ms, write 0x02 → release touch reset (IO1 high)
 *   4. Wait 50 ms for GT911 to boot
 *   5. Write 0x06 to 0x38  → backlight on  (IO1=1, IO2=1)
 *
 * References:
 *   • ESPHome CH422G driver source (api-docs.esphome.io/ch422g_8cpp_source)
 *   • https://medium.com/@pvginkel/brownout-issues-with-waveshares-esp32-s3-touch-lcd-4-3
 */

#include "bsp.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_IO";

/* CH422G uses different I2C addresses per internal register */
#define CH422G_ADDR_MODE    0x24   /**< Mode/config register  */
#define CH422G_ADDR_OUT     0x38   /**< GPIO output register  */

/* GPIO bit assignments – verified against official Waveshare board config */
#define CH422G_BIT_TOUCH_RST   (1U << BSP_IO_TOUCH_RST_PIN)   /* IO1, bit 1 – GT911 RST  */
#define CH422G_BIT_LCD_BL      (1U << BSP_IO_LCD_BL_PIN)      /* IO2, bit 2 – Backlight  */
#define CH422G_BIT_LCD_RST     (1U << 3)                       /* IO3, bit 3 – LCD RST    */

/* I2C device handles (two handles, two logical registers) */
static i2c_master_dev_handle_t s_mode_dev = NULL;
static i2c_master_dev_handle_t s_out_dev  = NULL;

/* Shadow of the current output register value */
static uint8_t s_out_val = 0x00;

/* ============================================================
 * Internal helpers
 * ============================================================ */
static esp_err_t ch422g_write_mode(uint8_t mode)
{
    return i2c_master_transmit(s_mode_dev, &mode, 1, pdMS_TO_TICKS(50));
}

static esp_err_t ch422g_write_out(uint8_t val)
{
    s_out_val = val;
    return i2c_master_transmit(s_out_dev, &val, 1, pdMS_TO_TICKS(50));
}

/* ============================================================
 * BSP functions
 * ============================================================ */
esp_err_t bsp_io_expander_init(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "Initialising CH422G (mode@0x%02X, out@0x%02X)",
             CH422G_ADDR_MODE, CH422G_ADDR_OUT);

    const i2c_device_config_t mode_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CH422G_ADDR_MODE,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &mode_cfg, &s_mode_dev));

    const i2c_device_config_t out_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CH422G_ADDR_OUT,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &out_cfg, &s_out_dev));

    /* Enable GPIO output mode on pins 0-7 */
    ESP_ERROR_CHECK(ch422g_write_mode(0x01));

    /* Start with all outputs low (touch in reset, backlight off) */
    ESP_ERROR_CHECK(ch422g_write_out(0x00));

    ESP_LOGI(TAG, "CH422G ready");
    return ESP_OK;
}

esp_err_t bsp_backlight_set(bool on)
{
    if (!s_out_dev) return ESP_ERR_INVALID_STATE;

    if (on) {
        return ch422g_write_out(s_out_val | CH422G_BIT_LCD_BL);
    } else {
        return ch422g_write_out(s_out_val & ~CH422G_BIT_LCD_BL);
    }
}

esp_err_t bsp_lcd_rst_pulse(void)
{
    if (!s_out_dev) return ESP_ERR_INVALID_STATE;

    /* Assert LCD RST (IO3 low – was already low from init, but be explicit) */
    ESP_ERROR_CHECK(ch422g_write_out(s_out_val & ~CH422G_BIT_LCD_RST));
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Release LCD RST (IO3 high) */
    ESP_ERROR_CHECK(ch422g_write_out(s_out_val | CH422G_BIT_LCD_RST));
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}

esp_err_t bsp_touch_reset_pulse(void)
{
    if (!s_out_dev) return ESP_ERR_INVALID_STATE;

    /*
     * GT911 address-select sequence (from official Waveshare board config):
     *   1. Drive INT LOW  – selects I2C address 0x5D when RST rises
     *   2. Wait 10 ms
     *   3. Assert RST LOW (IO1), hold 100 ms
     *   4. Release RST HIGH  – GT911 latches INT=LOW → address 0x5D
     *   5. Wait 200 ms for GT911 to complete its internal boot
     *   6. Release INT as a floating input (GT911 uses it as interrupt output)
     */
    const gpio_config_t int_out_cfg = {
        .pin_bit_mask  = (1ULL << BSP_TOUCH_INT_GPIO),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_out_cfg);
    gpio_set_level(BSP_TOUCH_INT_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_ERROR_CHECK(ch422g_write_out(s_out_val & ~CH422G_BIT_TOUCH_RST));
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(ch422g_write_out(s_out_val | CH422G_BIT_TOUCH_RST));
    vTaskDelay(pdMS_TO_TICKS(200));

    const gpio_config_t int_in_cfg = {
        .pin_bit_mask  = (1ULL << BSP_TOUCH_INT_GPIO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_in_cfg);

    return ESP_OK;
}
