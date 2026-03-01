/**
 * @file daq_wifi.c
 * @brief Wi-Fi connectivity + MQTT DAQ driver
 *
 * MQTT topic convention (configurable via daq_device_t):
 *
 *   Subscribe : <topic>        (e.g. "factory/line1/daq/#")
 *   Message   : same JSON as UART  {"id":…,"ch":[…]}
 *
 * Wi-Fi credentials are provided at call-time; typically loaded
 * from NVS by the application layer before daq_manager_start().
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "daq_manager.h"

static const char *TAG = "DAQ_WIFI";

/* ============================================================
 * Wi-Fi state
 * ============================================================ */
#define WIFI_CONNECTED_BIT   BIT0
#define WIFI_FAIL_BIT        BIT1
#define WIFI_MAX_RETRIES     10

static EventGroupHandle_t s_wifi_evg       = NULL;
static int                s_wifi_retry     = 0;
static bool               s_wifi_initialised = false;
static bool               s_wifi_started     = false;
static bool               s_wifi_stopping    = false;
static bool               s_wifi_connected   = false;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        if (s_wifi_stopping) {
            /* Intentional stop — suppress auto-retry */
        } else if (s_wifi_retry < WIFI_MAX_RETRIES) {
            esp_wifi_connect();
            s_wifi_retry++;
            ESP_LOGI(TAG, "Wi-Fi retry %d/%d", s_wifi_retry, WIFI_MAX_RETRIES);
        } else {
            xEventGroupSetBits(s_wifi_evg, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_retry = 0;
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_evg, WIFI_CONNECTED_BIT);
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Wi-Fi connected, IP: " IPSTR, IP2STR(&ev->ip_info.ip));
    }
}

esp_err_t daq_wifi_init(void)
{
    if (s_wifi_initialised) return ESP_OK;

    s_wifi_evg = xEventGroupCreate();
    if (!s_wifi_evg) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    s_wifi_initialised = true;
    ESP_LOGI(TAG, "Wi-Fi driver initialised");
    return ESP_OK;
}

esp_err_t daq_wifi_connect(const char *ssid, const char *password,
                           uint32_t timeout_ms)
{
    if (!s_wifi_initialised) {
        ESP_LOGE(TAG, "Call daq_wifi_init() before daq_wifi_connect()");
        return ESP_ERR_INVALID_STATE;
    }

    /* Tear down any previous session so we can apply fresh credentials */
    if (s_wifi_started) {
        s_wifi_stopping = true;
        esp_wifi_stop();
        s_wifi_stopping = false;
        s_wifi_started   = false;
        s_wifi_connected = false;
    }

    xEventGroupClearBits(s_wifi_evg, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_wifi_retry = 0;

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid,     ssid,     sizeof(wifi_cfg.sta.ssid)     - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable    = true;
    wifi_cfg.sta.pmf_cfg.required   = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_started = true;

    EventBits_t bits = xEventGroupWaitBits(s_wifi_evg,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;
    if (bits & WIFI_FAIL_BIT)      return ESP_ERR_WIFI_NOT_CONNECT;
    return ESP_ERR_TIMEOUT;
}

bool daq_wifi_is_connected(void) { return s_wifi_connected; }

/* ============================================================
 * MQTT driver context
 * ============================================================ */
typedef struct {
    esp_mqtt_client_handle_t client;
    daq_device_t            *dev;   /* back-pointer for event handler */
} mqtt_ctx_t;

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    mqtt_ctx_t *ctx = (mqtt_ctx_t *)arg;
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "[%s] MQTT connected", ctx->dev->id);
            ctx->dev->status = DAQ_STATUS_CONNECTED;
            esp_mqtt_client_subscribe(ctx->client,
                                      ctx->dev->conn.mqtt.topic, 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "[%s] MQTT disconnected", ctx->dev->id);
            ctx->dev->status = DAQ_STATUS_ERROR;
            break;

        case MQTT_EVENT_DATA: {
            /* Null-terminate the payload for cJSON */
            char *payload = strndup(ev->data, ev->data_len);
            if (payload) {
                cJSON *root = cJSON_Parse(payload);
                free(payload);
                if (root) {
                    cJSON *ch_arr = cJSON_GetObjectItemCaseSensitive(root, "ch");
                    if (cJSON_IsArray(ch_arr)) {
                        int n = cJSON_GetArraySize(ch_arr);
                        for (int i = 0; i < n && i < DAQ_MAX_CHANNELS; i++) {
                            cJSON *ch  = cJSON_GetArrayItem(ch_arr, i);
                            cJSON *cn  = cJSON_GetObjectItemCaseSensitive(ch, "n");
                            cJSON *cv  = cJSON_GetObjectItemCaseSensitive(ch, "v");
                            cJSON *cu  = cJSON_GetObjectItemCaseSensitive(ch, "u");
                            if (!cJSON_IsString(cn) || !cJSON_IsNumber(cv)) continue;
                            if (i >= ctx->dev->num_channels) {
                                strncpy(ctx->dev->channels[i].name,
                                        cn->valuestring, CH_NAME_LEN - 1);
                                if (cJSON_IsString(cu)) {
                                    strncpy(ctx->dev->channels[i].unit,
                                            cu->valuestring, CH_UNIT_LEN - 1);
                                }
                                ctx->dev->num_channels = i + 1;
                            }
                            daq_manager_push_value(ctx->dev, (uint8_t)i,
                                                   (float)cv->valuedouble);
                        }
                    }
                    cJSON_Delete(root);
                }
            }
            break;
        }

        default:
            break;
    }
}

/* ============================================================
 * Driver callbacks
 * ============================================================ */
esp_err_t daq_mqtt_connect(daq_device_t *dev)
{
    if (!s_wifi_connected) {
        ESP_LOGE(TAG, "[%s] Wi-Fi not connected – cannot start MQTT", dev->id);
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    mqtt_ctx_t *ctx = calloc(1, sizeof(mqtt_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;
    ctx->dev = dev;

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = dev->conn.mqtt.broker_uri,
        .credentials = {
            .client_id = dev->conn.mqtt.client_id[0] ?
                         dev->conn.mqtt.client_id : dev->id,
            .username  = dev->conn.mqtt.username,
            .authentication.password = dev->conn.mqtt.password,
        },
    };

    ctx->client = esp_mqtt_client_init(&mqtt_cfg);
    if (!ctx->client) { free(ctx); return ESP_FAIL; }

    esp_mqtt_client_register_event(ctx->client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, ctx);
    esp_err_t err = esp_mqtt_client_start(ctx->client);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(ctx->client);
        free(ctx);
        return err;
    }

    dev->drv_ctx = ctx;
    ESP_LOGI(TAG, "[%s] MQTT client started → %s / %s",
             dev->id, dev->conn.mqtt.broker_uri, dev->conn.mqtt.topic);
    return ESP_OK;
}

esp_err_t daq_mqtt_disconnect(daq_device_t *dev)
{
    mqtt_ctx_t *ctx = (mqtt_ctx_t *)dev->drv_ctx;
    if (!ctx) return ESP_OK;

    esp_mqtt_client_stop(ctx->client);
    esp_mqtt_client_destroy(ctx->client);
    free(ctx);
    dev->drv_ctx = NULL;
    return ESP_OK;
}
