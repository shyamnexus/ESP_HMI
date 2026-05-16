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

    /* Board support: I2C, IO expander, LCD panel, touch controller */
    ESP_LOGI(TAG, "Initialising BSP...");
    ESP_ERROR_CHECK(bsp_init());

    /* DAQ manager: UART and Wi-Fi/MQTT acquisition backends */
    ESP_LOGI(TAG, "Initialising DAQ manager...");
    ESP_ERROR_CHECK(daq_manager_init());

    /* HMI: LVGL task, screens, data binding */
    ESP_LOGI(TAG, "Initialising HMI...");
    ESP_ERROR_CHECK(hmi_init());

    /* Auto-connect Wi-Fi using credentials saved from the Settings screen */
    {
        nvs_handle_t nvs_h;
        char ssid[64] = {0}, pass[64] = {0};
        if (nvs_open("wifi_cfg", NVS_READONLY, &nvs_h) == ESP_OK) {
            size_t ssid_len = sizeof(ssid);
            size_t pass_len = sizeof(pass);
            nvs_get_str(nvs_h, "ssid", ssid, &ssid_len);
            nvs_get_str(nvs_h, "pass", pass, &pass_len);
            nvs_close(nvs_h);
        }
        if (ssid[0] != '\0') {
            ESP_LOGI(TAG, "Auto-connecting to Wi-Fi SSID: %s", ssid);
            esp_err_t wifi_err = daq_wifi_connect(ssid, pass, 15000);
            if (wifi_err == ESP_OK) {
                ESP_LOGI(TAG, "Wi-Fi connected");
            } else {
                ESP_LOGW(TAG, "Wi-Fi auto-connect failed (%s) – continuing without Wi-Fi",
                         esp_err_to_name(wifi_err));
            }
        } else {
            ESP_LOGI(TAG, "No saved Wi-Fi credentials – configure in Settings");
        }
    }

    /* Load saved DAQ device configurations from NVS */
    daq_manager_load_config();

    /* Start DAQ polling / MQTT subscriptions */
    ESP_ERROR_CHECK(daq_manager_start());

    /* Hand control to LVGL (runs indefinitely on core 1) */
    ESP_ERROR_CHECK(hmi_start());

    ESP_LOGI(TAG, "System running.");
}
