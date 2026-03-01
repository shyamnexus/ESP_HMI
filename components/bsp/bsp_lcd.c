#include "bsp.h"
#include "esp_log.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

static const char *TAG = "BSP_LCD";

/* Singleton populated by bsp_init() */
bsp_handles_t g_bsp_handles = {0};

/* Forward declarations from the other BSP source files */
esp_err_t bsp_io_expander_init(i2c_master_bus_handle_t bus);
esp_err_t bsp_lcd_rst_pulse(void);
esp_err_t bsp_touch_reset_pulse(void);
esp_err_t bsp_touch_init(i2c_master_bus_handle_t bus);

/* ============================================================
 * RGB panel initialisation
 * ============================================================ */
static esp_err_t lcd_panel_init(void)
{
    ESP_LOGI(TAG, "Initialising RGB LCD panel (%dx%d, pclk=%dHz)",
             BSP_LCD_H_RES, BSP_LCD_V_RES, BSP_LCD_PCLK_HZ);

    const esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src        = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz            = BSP_LCD_PCLK_HZ,
            .h_res              = BSP_LCD_H_RES,
            .v_res              = BSP_LCD_V_RES,
            .hsync_back_porch   = BSP_LCD_HSYNC_BP,
            .hsync_front_porch  = BSP_LCD_HSYNC_FP,
            .hsync_pulse_width  = BSP_LCD_HSYNC_PW,
            .vsync_back_porch   = BSP_LCD_VSYNC_BP,
            .vsync_front_porch  = BSP_LCD_VSYNC_FP,
            .vsync_pulse_width  = BSP_LCD_VSYNC_PW,
            .flags.pclk_active_neg = true,
        },
        .data_width     = 16,     /* RGB565 */
        .bits_per_pixel = 16,
        .num_fbs        = 2,      /* Double-buffered, allocated in PSRAM */
        /* Bounce buffer: small SRAM staging area while main fb lives in PSRAM.
         * 10 lines × 800 pixels × 2 bytes = 16 KB used from SRAM. */
        .bounce_buffer_size_px = 10 * BSP_LCD_H_RES,
        /* DMA burst size (power-of-2, added in ESP-IDF v5.3).
         * 64 bytes aligns well with the PSRAM cache-line on ESP32-S3. */
        .dma_burst_size = 64,
        .hsync_gpio_num = BSP_LCD_HSYNC_GPIO,
        .vsync_gpio_num = BSP_LCD_VSYNC_GPIO,
        .de_gpio_num    = BSP_LCD_DE_GPIO,
        .pclk_gpio_num  = BSP_LCD_PCLK_GPIO,
        .disp_gpio_num  = BSP_LCD_DISP_GPIO,
        .data_gpio_nums = {
            /* Bit  0 – 4 : Blue  [B3..B7] */
            BSP_LCD_B0_GPIO, BSP_LCD_B1_GPIO, BSP_LCD_B2_GPIO,
            BSP_LCD_B3_GPIO, BSP_LCD_B4_GPIO,
            /* Bit  5 – 10: Green [G2..G7] */
            BSP_LCD_G0_GPIO, BSP_LCD_G1_GPIO, BSP_LCD_G2_GPIO,
            BSP_LCD_G3_GPIO, BSP_LCD_G4_GPIO, BSP_LCD_G5_GPIO,
            /* Bit 11 – 15: Red   [R3..R7] */
            BSP_LCD_R0_GPIO, BSP_LCD_R1_GPIO, BSP_LCD_R2_GPIO,
            BSP_LCD_R3_GPIO, BSP_LCD_R4_GPIO,
        },
        .flags = {
            .fb_in_psram = true,   /* Framebuffers in 8 MB OPI PSRAM */
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &g_bsp_handles.lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_bsp_handles.lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_bsp_handles.lcd_panel));

    ESP_LOGI(TAG, "RGB panel ready");
    return ESP_OK;
}

/* ============================================================
 * Top-level BSP init (called once from app_main)
 * ============================================================ */
esp_err_t bsp_init(void)
{
    /* --- I2C master bus (shared by touch + IO expander) --- */
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port            = BSP_TOUCH_I2C_PORT,
        .sda_io_num          = BSP_TOUCH_SDA_GPIO,
        .scl_io_num          = BSP_TOUCH_SCL_GPIO,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &g_bsp_handles.i2c_bus));

    /* --- IO expander (CH422G): all outputs start low --- */
    ESP_ERROR_CHECK(bsp_io_expander_init(g_bsp_handles.i2c_bus));

    /* --- Release LCD RST (IO3) before panel init, per Waveshare reference --- */
    ESP_ERROR_CHECK(bsp_lcd_rst_pulse());

    /* --- RGB LCD panel --- */
    ESP_ERROR_CHECK(lcd_panel_init());

    /* --- GT911 touch reset sequence (INT low → RST pulse → INT float) --- */
    ESP_ERROR_CHECK(bsp_touch_reset_pulse());

    /* --- GT911 capacitive touch --- */
    ESP_ERROR_CHECK(bsp_touch_init(g_bsp_handles.i2c_bus));

    /* --- Turn backlight on after everything is ready --- */
    ESP_ERROR_CHECK(bsp_backlight_set(true));

    return ESP_OK;
}

const bsp_handles_t *bsp_get_handles(void)
{
    return &g_bsp_handles;
}
