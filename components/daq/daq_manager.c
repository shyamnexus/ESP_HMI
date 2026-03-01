#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "daq_manager.h"

static const char *TAG = "DAQ_MGR";

/* ============================================================
 * Internal driver interface – implemented in daq_uart.c / daq_wifi.c
 * ============================================================ */
esp_err_t daq_uart_connect(daq_device_t *dev);
esp_err_t daq_uart_poll(daq_device_t *dev);
esp_err_t daq_uart_disconnect(daq_device_t *dev);

esp_err_t daq_mqtt_connect(daq_device_t *dev);
esp_err_t daq_mqtt_disconnect(daq_device_t *dev);
/* MQTT is event-driven – no poll function needed */

esp_err_t daq_tcp_connect(daq_device_t *dev);
esp_err_t daq_tcp_poll(daq_device_t *dev);
esp_err_t daq_tcp_disconnect(daq_device_t *dev);

/* ============================================================
 * State
 * ============================================================ */
#define NVS_NAMESPACE       "daq_mgr"
#define NVS_KEY_DEV_COUNT   "dev_cnt"
#define NVS_KEY_DEV_FMT     "dev_%02d"

static daq_device_t  s_devices[DAQ_MAX_DEVICES];
static uint8_t       s_device_count  = 0;
static SemaphoreHandle_t s_lock      = NULL;
static TaskHandle_t  s_task_handle   = NULL;
static daq_update_cb_t s_update_cb   = NULL;
static void         *s_update_ctx    = NULL;

/* ============================================================
 * Helpers
 * ============================================================ */
static void channel_update(daq_device_t *dev, uint8_t ch_idx, float value)
{
    daq_channel_t *ch = &dev->channels[ch_idx];
    ch->value = value;
    ch->last_update_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    /* Running min/max */
    if (isnan(ch->value_min) || value < ch->value_min) ch->value_min = value;
    if (isnan(ch->value_max) || value > ch->value_max) ch->value_max = value;

    /* Push to history ring buffer */
    ch->history[ch->history_head] = value;
    ch->history_head = (ch->history_head + 1) % DAQ_HISTORY_DEPTH;
    if (ch->history_count < DAQ_HISTORY_DEPTH) ch->history_count++;

    /* Determine status from thresholds */
    if (!isnan(ch->alarm_lo) && value < ch->alarm_lo) {
        ch->status = CH_STATUS_ALARM;
    } else if (!isnan(ch->alarm_hi) && value > ch->alarm_hi) {
        ch->status = CH_STATUS_ALARM;
    } else if (!isnan(ch->warn_lo) && value < ch->warn_lo) {
        ch->status = CH_STATUS_WARNING;
    } else if (!isnan(ch->warn_hi) && value > ch->warn_hi) {
        ch->status = CH_STATUS_WARNING;
    } else {
        ch->status = CH_STATUS_NORMAL;
    }

    if (s_update_cb) {
        s_update_cb(dev, ch_idx, s_update_ctx);
    }
}

/* Exposed to drivers so they can push values without knowing the lock protocol */
void daq_manager_push_value(daq_device_t *dev, uint8_t ch_idx, float value)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    channel_update(dev, ch_idx, value);
    xSemaphoreGive(s_lock);
}

/* ============================================================
 * Polling task
 * ============================================================ */
static void daq_poll_task(void *arg)
{
    ESP_LOGI(TAG, "DAQ poll task started");

    while (true) {
        TickType_t now = xTaskGetTickCount();

        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (uint8_t i = 0; i < s_device_count; i++) {
            daq_device_t *dev = &s_devices[i];
            if (!dev->enabled || dev->status == DAQ_STATUS_DISCONNECTED) {
                continue;
            }

            esp_err_t err = ESP_OK;
            switch (dev->conn_type) {
                case DAQ_CONN_UART:
                    err = daq_uart_poll(dev);
                    break;
                case DAQ_CONN_WIFI_TCP:
                    err = daq_tcp_poll(dev);
                    break;
                case DAQ_CONN_WIFI_MQTT:
                    /* MQTT is event-driven; nothing to poll */
                    break;
                default:
                    break;
            }

            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Device %s poll error: %s", dev->id, esp_err_to_name(err));
                dev->status = DAQ_STATUS_ERROR;
            }

            /* Mark stale channels (no update for > 5 s) */
            uint32_t now_ms = (uint32_t)(now * portTICK_PERIOD_MS);
            for (uint8_t c = 0; c < dev->num_channels; c++) {
                daq_channel_t *ch = &dev->channels[c];
                if (ch->last_update_ms > 0 &&
                    (now_ms - ch->last_update_ms) > 5000) {
                    ch->status = CH_STATUS_STALE;
                }
            }
        }
        xSemaphoreGive(s_lock);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ============================================================
 * Connection task (runs once at startup then exits)
 * ============================================================ */
static void daq_connect_task(void *arg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (uint8_t i = 0; i < s_device_count; i++) {
        daq_device_t *dev = &s_devices[i];
        if (!dev->enabled) continue;

        dev->status = DAQ_STATUS_CONNECTING;
        esp_err_t err = ESP_OK;

        switch (dev->conn_type) {
            case DAQ_CONN_UART:      err = daq_uart_connect(dev);  break;
            case DAQ_CONN_WIFI_TCP:  err = daq_tcp_connect(dev);   break;
            case DAQ_CONN_WIFI_MQTT: err = daq_mqtt_connect(dev);  break;
            default: break;
        }

        dev->status = (err == ESP_OK) ? DAQ_STATUS_CONNECTED : DAQ_STATUS_ERROR;
        ESP_LOGI(TAG, "Device %s: %s", dev->id,
                 (err == ESP_OK) ? "connected" : "connection failed");
    }
    xSemaphoreGive(s_lock);
    vTaskDelete(NULL);
}

/* ============================================================
 * Public API
 * ============================================================ */
esp_err_t daq_manager_init(void)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    memset(s_devices, 0, sizeof(s_devices));
    s_device_count = 0;

    /* Initialise thresholds to NaN (disabled) */
    for (int i = 0; i < DAQ_MAX_DEVICES; i++) {
        for (int c = 0; c < DAQ_MAX_CHANNELS; c++) {
            s_devices[i].channels[c].warn_lo  = NAN;
            s_devices[i].channels[c].warn_hi  = NAN;
            s_devices[i].channels[c].alarm_lo = NAN;
            s_devices[i].channels[c].alarm_hi = NAN;
            s_devices[i].channels[c].value_min = NAN;
            s_devices[i].channels[c].value_max = NAN;
        }
    }

    ESP_LOGI(TAG, "DAQ manager initialised (max %d devices, %d ch/dev)",
             DAQ_MAX_DEVICES, DAQ_MAX_CHANNELS);
    return ESP_OK;
}

esp_err_t daq_manager_start(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;

    /* Connect all devices in a one-shot task */
    xTaskCreatePinnedToCore(daq_connect_task, "daq_connect", 4096,
                            NULL, 5, NULL, 0);

    /* Start the polling loop on core 0 */
    xTaskCreatePinnedToCore(daq_poll_task, "daq_poll", 4096,
                            NULL, 4, &s_task_handle, 0);
    return ESP_OK;
}

esp_err_t daq_manager_stop(void)
{
    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (uint8_t i = 0; i < s_device_count; i++) {
        daq_device_t *dev = &s_devices[i];
        switch (dev->conn_type) {
            case DAQ_CONN_UART:      daq_uart_disconnect(dev);  break;
            case DAQ_CONN_WIFI_TCP:  daq_tcp_disconnect(dev);   break;
            case DAQ_CONN_WIFI_MQTT: daq_mqtt_disconnect(dev);  break;
            default: break;
        }
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t daq_manager_add_device(const daq_device_t *dev, uint8_t *out_idx)
{
    if (s_device_count >= DAQ_MAX_DEVICES) return ESP_ERR_NO_MEM;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(&s_devices[s_device_count], dev, sizeof(daq_device_t));
    if (out_idx) *out_idx = s_device_count;
    s_device_count++;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t daq_manager_remove_device(uint8_t idx)
{
    if (idx >= s_device_count) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* Shift remaining entries */
    for (uint8_t i = idx; i < s_device_count - 1; i++) {
        s_devices[i] = s_devices[i + 1];
    }
    s_device_count--;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void daq_manager_lock(void)    { xSemaphoreTake(s_lock, portMAX_DELAY); }
void daq_manager_unlock(void)  { xSemaphoreGive(s_lock); }
uint8_t daq_manager_get_device_count(void) { return s_device_count; }

const daq_device_t *daq_manager_get_device(uint8_t idx)
{
    if (idx >= s_device_count) return NULL;
    return &s_devices[idx];
}

void daq_manager_set_update_cb(daq_update_cb_t cb, void *user_ctx)
{
    s_update_cb  = cb;
    s_update_ctx = user_ctx;
}

esp_err_t daq_manager_set_thresholds(uint8_t dev_idx, uint8_t ch_idx,
                                     float warn_lo, float warn_hi,
                                     float alarm_lo, float alarm_hi)
{
    if (dev_idx >= s_device_count || ch_idx >= s_devices[dev_idx].num_channels) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    daq_channel_t *ch = &s_devices[dev_idx].channels[ch_idx];
    ch->warn_lo  = warn_lo;
    ch->warn_hi  = warn_hi;
    ch->alarm_lo = alarm_lo;
    ch->alarm_hi = alarm_hi;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

/* ============================================================
 * NVS persistence
 * ============================================================ */
esp_err_t daq_manager_save_config(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    nvs_set_u8(nvs, NVS_KEY_DEV_COUNT, s_device_count);
    for (uint8_t i = 0; i < s_device_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), NVS_KEY_DEV_FMT, i);
        /* Store only the connection config, not runtime state */
        daq_device_t tmp = s_devices[i];
        tmp.status    = DAQ_STATUS_DISCONNECTED;
        tmp.drv_ctx   = NULL;
        tmp.num_channels = 0; /* Channels are discovered at runtime */
        nvs_set_blob(nvs, key, &tmp, sizeof(tmp));
    }
    nvs_commit(nvs);

    xSemaphoreGive(s_lock);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Config saved (%d devices)", s_device_count);
    return ESP_OK;
}

esp_err_t daq_manager_load_config(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config found, using defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    uint8_t count = 0;
    nvs_get_u8(nvs, NVS_KEY_DEV_COUNT, &count);

    for (uint8_t i = 0; i < count && i < DAQ_MAX_DEVICES; i++) {
        char key[16];
        snprintf(key, sizeof(key), NVS_KEY_DEV_FMT, i);
        daq_device_t tmp = {0};
        size_t sz = sizeof(tmp);
        if (nvs_get_blob(nvs, key, &tmp, &sz) == ESP_OK) {
            daq_manager_add_device(&tmp, NULL);
        }
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded %d device(s) from NVS", count);
    return ESP_OK;
}
