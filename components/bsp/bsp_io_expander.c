#include "bsp.h"
#include "esp_log.h"
#include "esp_io_expander_ch422g.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_IO";

/* Forward-declared from bsp_lcd.c / bsp_touch.c via the shared handles struct */
extern bsp_handles_t g_bsp_handles;

esp_err_t bsp_io_expander_init(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "Initialising CH422G IO expander (addr=0x%02X)", BSP_IO_EXPANDER_ADDR);

    esp_err_t ret = esp_io_expander_new_i2c_ch422g(
        bus,
        BSP_IO_EXPANDER_ADDR,
        &g_bsp_handles.io_expander
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CH422G init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure both pins as outputs */
    uint32_t out_mask = (1U << BSP_IO_TOUCH_RST_PIN) | (1U << BSP_IO_LCD_BL_PIN);
    ESP_ERROR_CHECK(esp_io_expander_set_dir(g_bsp_handles.io_expander, out_mask, IO_EXPANDER_OUTPUT));

    /* Start with backlight off and touch in reset */
    ESP_ERROR_CHECK(esp_io_expander_set_level(g_bsp_handles.io_expander, out_mask, 0));

    ESP_LOGI(TAG, "CH422G ready");
    return ESP_OK;
}

esp_err_t bsp_backlight_set(bool on)
{
    if (!g_bsp_handles.io_expander) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_io_expander_set_level(
        g_bsp_handles.io_expander,
        (1U << BSP_IO_LCD_BL_PIN),
        on ? 1 : 0
    );
}

esp_err_t bsp_touch_reset_pulse(void)
{
    if (!g_bsp_handles.io_expander) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Pull RST low for 10 ms, then release */
    ESP_ERROR_CHECK(esp_io_expander_set_level(
        g_bsp_handles.io_expander, (1U << BSP_IO_TOUCH_RST_PIN), 0));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(esp_io_expander_set_level(
        g_bsp_handles.io_expander, (1U << BSP_IO_TOUCH_RST_PIN), 1));
    vTaskDelay(pdMS_TO_TICKS(50));   /* Allow GT911 to boot */
    return ESP_OK;
}
