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
 * On the 4.3B board the relevant output bits are:
 *   bit 0  –  EXIO1  (GT911 touch-reset, active-low)
 *   bit 1  –  EXIO2  (LCD backlight enable, active-high)
 *
 * Sequence to enable all peripherals:
 *   1. Write 0x01 to 0x24  → GPIO output mode
 *   2. Write 0x00 to 0x38  → all outputs low  (touch in reset)
 *   3. Wait 10 ms, write 0x01 → release touch reset (EXIO1 high)
 *   4. Wait 50 ms for GT911 to boot
 *   5. Write 0x03 to 0x38  → backlight on  (EXIO1=1, EXIO2=1)
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

/* GPIO bit assignments on EXIO header */
#define CH422G_BIT_TOUCH_RST   (1U << BSP_IO_TOUCH_RST_PIN)   /* EXIO1, bit 0 */
#define CH422G_BIT_LCD_BL      (1U << BSP_IO_LCD_BL_PIN)      /* EXIO2, bit 1 */

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

esp_err_t bsp_touch_reset_pulse(void)
{
    if (!s_out_dev) return ESP_ERR_INVALID_STATE;

    /*
     * GT911 I2C address selection:
     *   INT = LOW  while RST rises  →  address 0x5D (ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS)
     *   INT = HIGH while RST rises  →  address 0x14 (backup)
     *
     * The board has a pull-up on the INT line, so without explicit control the
     * GT911 would boot at 0x14, causing an I2C NAK when the driver tries 0x5D.
     * Drive INT output-low before asserting reset so we get a known address.
     */
    const gpio_config_t int_out_cfg = {
        .pin_bit_mask    = (1ULL << BSP_TOUCH_INT_GPIO),
        .mode            = GPIO_MODE_OUTPUT,
        .pull_up_en      = GPIO_PULLUP_DISABLE,
        .pull_down_en    = GPIO_PULLDOWN_DISABLE,
        .intr_type       = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_out_cfg);
    gpio_set_level(BSP_TOUCH_INT_GPIO, 0);

    /* Assert reset (EXIO1 low) */
    ESP_ERROR_CHECK(ch422g_write_out(s_out_val & ~CH422G_BIT_TOUCH_RST));
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Release reset; GT911 latches address on the RST rising edge */
    ESP_ERROR_CHECK(ch422g_write_out(s_out_val | CH422G_BIT_TOUCH_RST));

    /* Hold INT low for ≥ 5 ms after RST rises so the address is firmly latched */
    vTaskDelay(pdMS_TO_TICKS(5));

    /* Release INT – float as input so GT911 can drive it as an interrupt output */
    const gpio_config_t int_in_cfg = {
        .pin_bit_mask    = (1ULL << BSP_TOUCH_INT_GPIO),
        .mode            = GPIO_MODE_INPUT,
        .pull_up_en      = GPIO_PULLUP_DISABLE,
        .pull_down_en    = GPIO_PULLDOWN_DISABLE,
        .intr_type       = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_in_cfg);

    vTaskDelay(pdMS_TO_TICKS(50));   /* GT911 internal boot time */

    return ESP_OK;
}
