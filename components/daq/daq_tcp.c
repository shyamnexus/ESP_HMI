/**
 * @file daq_tcp.c
 * @brief TCP DAQ driver
 *
 * Wire protocol (same as UART, but over a TCP socket):
 *
 *   Host → Device  : "READ\n"
 *   Device → Host  : {"id":"DAQ1","ch":[{"n":"V1","v":3.14,"u":"V"},...]}\n
 *
 * The driver opens a persistent TCP connection to dev->conn.tcp.host:port.
 * If the connection drops it marks the device as ERROR; the application can
 * call daq_tcp_connect() again to re-establish it.
 */

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"
#include "daq_manager.h"

static const char *TAG = "DAQ_TCP";

#define TCP_BUF_SIZE      2048
#define TCP_TIMEOUT_SEC   2

typedef struct {
    int    sock_fd;
    char   rx_buf[TCP_BUF_SIZE];
    size_t rx_len;
} tcp_ctx_t;

/* ============================================================
 * JSON frame parser (mirrors daq_uart.c)
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

        if (i >= dev->num_channels) {
            strncpy(dev->channels[i].name, n->valuestring, CH_NAME_LEN - 1);
            if (cJSON_IsString(u)) {
                strncpy(dev->channels[i].unit, u->valuestring, CH_UNIT_LEN - 1);
            }
            dev->num_channels = i + 1;
        }

        daq_manager_push_value(dev, (uint8_t)i, (float)v->valuedouble);
    }

    cJSON_Delete(root);
}

/* ============================================================
 * Driver callbacks
 * ============================================================ */
esp_err_t daq_tcp_connect(daq_device_t *dev)
{
    tcp_ctx_t *ctx = calloc(1, sizeof(tcp_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;
    ctx->sock_fd = -1;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", dev->conn.tcp.port);

    struct addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;

    int gai_err = getaddrinfo(dev->conn.tcp.host, port_str, &hints, &res);
    if (gai_err != 0 || !res) {
        ESP_LOGE(TAG, "[%s] DNS lookup failed for '%s': %d",
                 dev->id, dev->conn.tcp.host, gai_err);
        free(ctx);
        return ESP_FAIL;
    }

    int sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "[%s] socket() failed: %d", dev->id, errno);
        freeaddrinfo(res);
        free(ctx);
        return ESP_FAIL;
    }

    struct timeval tv = { .tv_sec = TCP_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "[%s] connect() to %s:%u failed: %d",
                 dev->id, dev->conn.tcp.host, dev->conn.tcp.port, errno);
        close(sock);
        freeaddrinfo(res);
        free(ctx);
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    freeaddrinfo(res);
    ctx->sock_fd = sock;
    dev->drv_ctx = ctx;

    ESP_LOGI(TAG, "[%s] TCP connected to %s:%u",
             dev->id, dev->conn.tcp.host, dev->conn.tcp.port);
    return ESP_OK;
}

esp_err_t daq_tcp_poll(daq_device_t *dev)
{
    tcp_ctx_t *ctx = (tcp_ctx_t *)dev->drv_ctx;
    if (!ctx || ctx->sock_fd < 0) return ESP_ERR_INVALID_STATE;

    int sent = send(ctx->sock_fd, "READ\n", 5, 0);
    if (sent < 0) {
        ESP_LOGW(TAG, "[%s] send() failed: %d", dev->id, errno);
        close(ctx->sock_fd);
        ctx->sock_fd = -1;
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    /* Read byte-by-byte until newline or timeout */
    char byte;
    while (true) {
        int n = recv(ctx->sock_fd, &byte, 1, 0);
        if (n <= 0) {
            /* Timeout or connection closed */
            if (ctx->rx_len > 0) {
                ctx->rx_buf[ctx->rx_len] = '\0';
                parse_frame(dev, ctx->rx_buf);
                ctx->rx_len = 0;
            }
            if (n == 0) {
                /* Remote end closed the connection */
                ESP_LOGW(TAG, "[%s] TCP connection closed by remote", dev->id);
                close(ctx->sock_fd);
                ctx->sock_fd = -1;
                return ESP_ERR_WIFI_NOT_CONNECT;
            }
            ESP_LOGW(TAG, "[%s] TCP recv timeout", dev->id);
            return ESP_ERR_TIMEOUT;
        }

        if (byte == '\n') {
            if (ctx->rx_len > 0) {
                ctx->rx_buf[ctx->rx_len] = '\0';
                parse_frame(dev, ctx->rx_buf);
                ctx->rx_len = 0;
            }
            return ESP_OK;
        }

        if (ctx->rx_len < TCP_BUF_SIZE - 1) {
            ctx->rx_buf[ctx->rx_len++] = byte;
        } else {
            /* Buffer overflow – discard and start fresh */
            ctx->rx_len = 0;
        }
    }
}

esp_err_t daq_tcp_disconnect(daq_device_t *dev)
{
    tcp_ctx_t *ctx = (tcp_ctx_t *)dev->drv_ctx;
    if (!ctx) return ESP_OK;

    if (ctx->sock_fd >= 0) {
        close(ctx->sock_fd);
        ctx->sock_fd = -1;
    }
    free(ctx);
    dev->drv_ctx = NULL;
    return ESP_OK;
}
