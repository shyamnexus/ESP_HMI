#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Limits
 * ============================================================ */
#define DAQ_MAX_DEVICES         8
#define DAQ_MAX_CHANNELS        32
#define DAQ_HISTORY_DEPTH       120    /* samples kept per channel (ring buffer) */

#define DAQ_ID_LEN              16
#define DAQ_NAME_LEN            32
#define DAQ_HOST_LEN            64
#define DAQ_TOPIC_LEN           128
#define CH_NAME_LEN             24
#define CH_UNIT_LEN             12

/* ============================================================
 * DAQ connection type
 * ============================================================ */
typedef enum {
    DAQ_CONN_UART      = 0,   /* RS-232 / RS-485 via UART port           */
    DAQ_CONN_WIFI_TCP  = 1,   /* Raw TCP socket over Wi-Fi               */
    DAQ_CONN_WIFI_MQTT = 2,   /* MQTT broker over Wi-Fi                  */
} daq_conn_type_t;

/* ============================================================
 * Device and channel status
 * ============================================================ */
typedef enum {
    DAQ_STATUS_DISCONNECTED = 0,
    DAQ_STATUS_CONNECTING,
    DAQ_STATUS_CONNECTED,
    DAQ_STATUS_ERROR,
} daq_status_t;

typedef enum {
    CH_STATUS_NORMAL  = 0,
    CH_STATUS_WARNING,
    CH_STATUS_ALARM,
    CH_STATUS_STALE,    /* No update received within the stale timeout */
} ch_status_t;

/* ============================================================
 * Channel descriptor
 * ============================================================ */
typedef struct {
    char          name[CH_NAME_LEN];
    char          unit[CH_UNIT_LEN];

    float         value;
    float         value_min;         /* Running minimum since last reset  */
    float         value_max;         /* Running maximum since last reset  */

    /* Configurable alarm thresholds (NaN = disabled) */
    float         warn_lo;
    float         warn_hi;
    float         alarm_lo;
    float         alarm_hi;

    ch_status_t   status;
    uint32_t      last_update_ms;    /* esp_log_timestamp() at last update */

    /* Ring buffer for sparkline / chart data */
    float         history[DAQ_HISTORY_DEPTH];
    uint8_t       history_head;      /* next write position */
    uint16_t      history_count;     /* valid samples (0..DAQ_HISTORY_DEPTH) */
} daq_channel_t;

/* ============================================================
 * Connection parameters (tagged union)
 * ============================================================ */
typedef struct {
    int  port_num;       /* UART_NUM_0, UART_NUM_1, … */
    int  baud_rate;      /* e.g. 115200 */
} daq_uart_params_t;

typedef struct {
    char     host[DAQ_HOST_LEN];
    uint16_t port;
} daq_tcp_params_t;

typedef struct {
    char     broker_uri[DAQ_HOST_LEN + 8];  /* e.g. "mqtt://192.168.1.10:1883" */
    char     topic[DAQ_TOPIC_LEN];           /* Subscribe topic, may contain '+' */
    char     client_id[DAQ_ID_LEN];
    char     username[DAQ_NAME_LEN];
    char     password[DAQ_NAME_LEN];
} daq_mqtt_params_t;

/* ============================================================
 * DAQ device descriptor
 * ============================================================ */
typedef struct {
    char              id[DAQ_ID_LEN];       /* Unique device identifier */
    char              name[DAQ_NAME_LEN];   /* Human-readable name      */
    daq_conn_type_t   conn_type;

    daq_status_t      status;
    bool              enabled;

    uint8_t           num_channels;
    daq_channel_t     channels[DAQ_MAX_CHANNELS];

    /* Poll interval (UART / TCP only; MQTT is event-driven) */
    uint32_t          poll_interval_ms;

    union {
        daq_uart_params_t  uart;
        daq_tcp_params_t   tcp;
        daq_mqtt_params_t  mqtt;
    } conn;

    /* Driver private context – allocated/freed by the driver */
    void             *drv_ctx;
} daq_device_t;

#ifdef __cplusplus
}
#endif
