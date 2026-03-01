#pragma once

#include "esp_err.h"
#include "daq_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Callback invoked from the DAQ manager task each time
 * any channel value is updated.  The callback runs in the
 * DAQ task context, so keep it short (set a flag / post to
 * a queue rather than doing heavy work).
 * ============================================================ */
typedef void (*daq_update_cb_t)(const daq_device_t *dev, uint8_t channel_idx, void *user_ctx);

/* ============================================================
 * Lifecycle
 * ============================================================ */

/** Allocate internal state.  Call once before anything else. */
esp_err_t daq_manager_init(void);

/** Start the DAQ polling / MQTT subscription task. */
esp_err_t daq_manager_start(void);

/** Stop all acquisition and free resources. */
esp_err_t daq_manager_stop(void);

/* ============================================================
 * Configuration
 * ============================================================ */

/**
 * @brief Add a new DAQ device.
 *
 * @param[in]  dev       Fully initialised descriptor (copied internally).
 * @param[out] out_idx   Index assigned to this device (0 … DAQ_MAX_DEVICES-1).
 */
esp_err_t daq_manager_add_device(const daq_device_t *dev, uint8_t *out_idx);

/** Remove a device by index; stops its acquisition first. */
esp_err_t daq_manager_remove_device(uint8_t idx);

/** Persist all device configs to NVS. */
esp_err_t daq_manager_save_config(void);

/** Restore device configs from NVS; silently succeeds if nothing was saved. */
esp_err_t daq_manager_load_config(void);

/* ============================================================
 * Data access  (take / release lock around multi-field reads)
 * ============================================================ */

/** Acquire the data lock before reading any daq_device_t fields. */
void daq_manager_lock(void);

/** Release the data lock. */
void daq_manager_unlock(void);

/** Return the number of registered devices. */
uint8_t daq_manager_get_device_count(void);

/**
 * @brief Return a const pointer to a device descriptor.
 *
 * Must be called with the lock held.
 * Returns NULL if @p idx is out of range.
 */
const daq_device_t *daq_manager_get_device(uint8_t idx);

/** Register a callback invoked on every channel update. */
void daq_manager_set_update_cb(daq_update_cb_t cb, void *user_ctx);

/**
 * @brief Push a new value for a specific channel (called by drivers).
 *
 * Acquires the data lock internally, updates the ring buffer, evaluates
 * thresholds, and invokes the registered update callback.
 */
void daq_manager_push_value(daq_device_t *dev, uint8_t ch_idx, float value);

/* ============================================================
 * Threshold configuration helpers
 * ============================================================ */

esp_err_t daq_manager_set_thresholds(uint8_t dev_idx, uint8_t ch_idx,
                                     float warn_lo, float warn_hi,
                                     float alarm_lo, float alarm_hi);

/* ============================================================
 * Wi-Fi helpers (shared between UART and MQTT DAQ)
 * ============================================================ */

/** Connect to Wi-Fi using credentials stored in NVS.
 *  Blocks until connected or timeout_ms elapses. */
esp_err_t daq_wifi_connect(const char *ssid, const char *password,
                           uint32_t timeout_ms);

/** Return true if Wi-Fi station is connected with an IP address. */
bool daq_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
