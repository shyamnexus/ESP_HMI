#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "bsp.h"
#include "hmi.h"
#include "daq_manager.h"

static const char *TAG = "MAIN";

static esp_err_t nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP HMI for DAQ Systems ===");
    ESP_LOGI(TAG, "Board: Waveshare ESP32-S3-Touch-LCD-4.3B");
    ESP_LOGI(TAG, "Display: 800x480 RGB LCD");

    /* NVS flash for configuration persistence */
    ESP_ERROR_CHECK(nvs_init());

    /* Default event loop (required by Wi-Fi, MQTT, etc.) */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Wi-Fi driver must run first to claim its DMA-only SRAM buffers
     * (static RX, mgmt, TX management) before the RGB LCD driver
     * allocates its own DMA bounce buffers (2 × 16 KB).  Reversing this
     * order exhausts the internal DMA heap and causes esp_wifi_init() to
     * return ESP_ERR_NO_MEM. */
    ESP_LOGI(TAG, "Initialising Wi-Fi driver...");
    ESP_ERROR_CHECK(daq_wifi_init());

    /* DAQ manager: UART and Wi-Fi/MQTT acquisition backends */
    ESP_LOGI(TAG, "Initialising DAQ manager...");
    ESP_ERROR_CHECK(daq_manager_init());

    /* Board support: I2C, IO expander, LCD panel, touch controller.
     * Runs after Wi-Fi so the LCD bounce buffers do not pre-empt the
     * Wi-Fi DMA pool. */
    ESP_LOGI(TAG, "Initialising BSP...");
    ESP_ERROR_CHECK(bsp_init());

    /* HMI: LVGL task, screens, data binding */
    ESP_LOGI(TAG, "Initialising HMI...");
    ESP_ERROR_CHECK(hmi_init());

    /* Load saved DAQ device configurations from NVS */
    daq_manager_load_config();

    /* Start DAQ polling / MQTT subscriptions */
    ESP_ERROR_CHECK(daq_manager_start());

    /* Hand control to LVGL (runs indefinitely on core 1) */
    ESP_ERROR_CHECK(hmi_start());

    ESP_LOGI(TAG, "System running.");
}
