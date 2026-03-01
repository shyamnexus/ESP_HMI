/**
 * @file daq_uart.c
 * @brief UART DAQ driver
 *
 * Wire protocol (ASCII, newline-terminated JSON):
 *
 *   Host → Device  : "READ\n"
 *   Device → Host  : {"id":"DAQ1","ch":[{"n":"V1","v":3.14,"u":"V"},...]}\n
 *
 * The device may also send unsolicited frames at any time; the driver
 * accepts both polled responses and unsolicited frames.
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "cJSON.h"
#include "daq_manager.h"

static const char *TAG = "DAQ_UART";

#define UART_BUF_SIZE     2048
#define UART_READ_TIMEOUT pdMS_TO_TICKS(500)

typedef struct {
    uart_port_t  port;
    char         rx_buf[UART_BUF_SIZE];
    size_t       rx_len;
} uart_ctx_t;

/* ============================================================
 * JSON frame parser
 * ============================================================ */
static void parse_frame(daq_device_t *dev, const char *json_str)
{
    cJSON *root = cJSON_ParseWithLength(json_str, strlen(json_str));
    if (!root) {
        ESP_LOGW(TAG, "[%s] JSON parse error", dev->id);
        return;
    }

    cJSON *ch_arr = cJSON_GetObjectItemCaseSensitive(root, "ch");
    if (!cJSON_IsArray(ch_arr)) {
        cJSON_Delete(root);
        return;
    }

    int arr_size = cJSON_GetArraySize(ch_arr);
    for (int i = 0; i < arr_size && i < DAQ_MAX_CHANNELS; i++) {
        cJSON *ch_obj = cJSON_GetArrayItem(ch_arr, i);
        cJSON *n = cJSON_GetObjectItemCaseSensitive(ch_obj, "n");
        cJSON *v = cJSON_GetObjectItemCaseSensitive(ch_obj, "v");
        cJSON *u = cJSON_GetObjectItemCaseSensitive(ch_obj, "u");

        if (!cJSON_IsString(n) || !cJSON_IsNumber(v)) continue;

        /* Grow channel list dynamically on first appearance */
        if (i >= dev->num_channels) {
            if (i < DAQ_MAX_CHANNELS) {
                strncpy(dev->channels[i].name, n->valuestring, CH_NAME_LEN - 1);
                if (cJSON_IsString(u)) {
                    strncpy(dev->channels[i].unit, u->valuestring, CH_UNIT_LEN - 1);
                }
                dev->num_channels = i + 1;
            }
        }

        daq_manager_push_value(dev, (uint8_t)i, (float)v->valuedouble);
    }

    cJSON_Delete(root);
}

/* ============================================================
 * Driver callbacks
 * ============================================================ */
esp_err_t daq_uart_connect(daq_device_t *dev)
{
    uart_ctx_t *ctx = calloc(1, sizeof(uart_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;

    ctx->port = (uart_port_t)dev->conn.uart.port_num;

    const uart_config_t uart_cfg = {
        .baud_rate  = dev->conn.uart.baud_rate,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(ctx->port, &uart_cfg);
    if (err != ESP_OK) { free(ctx); return err; }

    err = uart_driver_install(ctx->port, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) { free(ctx); return err; }

    dev->drv_ctx = ctx;
    ESP_LOGI(TAG, "[%s] UART%d connected at %d baud",
             dev->id, ctx->port, dev->conn.uart.baud_rate);
    return ESP_OK;
}

esp_err_t daq_uart_poll(daq_device_t *dev)
{
    uart_ctx_t *ctx = (uart_ctx_t *)dev->drv_ctx;
    if (!ctx) return ESP_ERR_INVALID_STATE;

    /* Send read request */
    uart_write_bytes(ctx->port, "READ\n", 5);

    /* Accumulate data until newline or timeout */
    uint8_t byte;
    TickType_t deadline = xTaskGetTickCount() + UART_READ_TIMEOUT;

    while (xTaskGetTickCount() < deadline) {
        int n = uart_read_bytes(ctx->port, &byte, 1, pdMS_TO_TICKS(10));
        if (n <= 0) continue;

        if (byte == '\n') {
            if (ctx->rx_len > 0) {
                ctx->rx_buf[ctx->rx_len] = '\0';
                parse_frame(dev, ctx->rx_buf);
                ctx->rx_len = 0;
            }
            return ESP_OK;
        }

        if (ctx->rx_len < UART_BUF_SIZE - 1) {
            ctx->rx_buf[ctx->rx_len++] = (char)byte;
        } else {
            /* Buffer overflow – reset and try again */
            ctx->rx_len = 0;
        }
    }

    ESP_LOGW(TAG, "[%s] UART poll timeout", dev->id);
    ctx->rx_len = 0;
    return ESP_ERR_TIMEOUT;
}

esp_err_t daq_uart_disconnect(daq_device_t *dev)
{
    uart_ctx_t *ctx = (uart_ctx_t *)dev->drv_ctx;
    if (!ctx) return ESP_OK;

    uart_driver_delete(ctx->port);
    free(ctx);
    dev->drv_ctx = NULL;
    return ESP_OK;
}

/* ============================================================
 * TCP stubs (minimal implementation – extend as needed)
 * ============================================================ */
esp_err_t daq_tcp_connect(daq_device_t *dev)
{
    ESP_LOGW(TAG, "[%s] TCP DAQ not yet implemented", dev->id);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t daq_tcp_poll(daq_device_t *dev)   { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t daq_tcp_disconnect(daq_device_t *dev) { return ESP_OK; }
