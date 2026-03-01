#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "daq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Colour palette  (dark DAQ theme)
 * ============================================================ */
#define HMI_COL_BG          lv_color_hex(0x0D1117)   /* Background            */
#define HMI_COL_SURFACE     lv_color_hex(0x161B22)   /* Card / panel surface  */
#define HMI_COL_SURFACE2    lv_color_hex(0x21262D)   /* Elevated surface      */
#define HMI_COL_BORDER      lv_color_hex(0x30363D)   /* Subtle border         */
#define HMI_COL_PRIMARY     lv_color_hex(0x58A6FF)   /* Primary accent (blue) */
#define HMI_COL_SUCCESS     lv_color_hex(0x3FB950)   /* Normal / connected    */
#define HMI_COL_WARNING     lv_color_hex(0xD29922)   /* Warning               */
#define HMI_COL_ALARM       lv_color_hex(0xF85149)   /* Alarm / error         */
#define HMI_COL_TEXT        lv_color_hex(0xE6EDF3)   /* Primary text          */
#define HMI_COL_TEXT_MUTED  lv_color_hex(0x7D8590)   /* Secondary / muted     */
#define HMI_COL_NAV_ACTIVE  lv_color_hex(0x58A6FF)   /* Active nav tab        */
#define HMI_COL_NAV_BG     lv_color_hex(0x010409)   /* Nav bar background    */

/* ============================================================
 * Layout constants
 * ============================================================ */
#define HMI_DISPLAY_W       800
#define HMI_DISPLAY_H       480
#define HMI_TOPBAR_H        40
#define HMI_NAVBAR_H        60
#define HMI_CONTENT_H       (HMI_DISPLAY_H - HMI_TOPBAR_H - HMI_NAVBAR_H)
#define HMI_CARD_PAD        8
#define HMI_CARD_RADIUS     10

/* ============================================================
 * Navigation panel IDs
 * ============================================================ */
typedef enum {
    HMI_PANEL_DASHBOARD = 0,
    HMI_PANEL_DEVICES,
    HMI_PANEL_SETTINGS,
    HMI_PANEL_CHANNEL,    /* Full-screen channel detail (no nav tab) */
    HMI_PANEL_COUNT,
} hmi_panel_t;

/* ============================================================
 * Screen-local build functions (called from hmi_main.c)
 * ============================================================ */
lv_obj_t *screen_dashboard_create(lv_obj_t *parent);
void      screen_dashboard_refresh(void);

lv_obj_t *screen_devices_create(lv_obj_t *parent);
void      screen_devices_refresh(void);

lv_obj_t *screen_channel_create(lv_obj_t *parent);
void      screen_channel_show(uint8_t dev_idx, uint8_t ch_idx);

lv_obj_t *screen_settings_create(lv_obj_t *parent);

/* ============================================================
 * Navigation
 * ============================================================ */
void hmi_navigate(hmi_panel_t panel);

/* ============================================================
 * Style helpers (shared across screens)
 * ============================================================ */
void hmi_style_card(lv_obj_t *obj);
void hmi_style_btn_primary(lv_obj_t *btn);
void hmi_style_label_title(lv_obj_t *label);
void hmi_style_label_value(lv_obj_t *label);
void hmi_style_label_unit(lv_obj_t *label);
void hmi_style_label_muted(lv_obj_t *label);

lv_color_t hmi_status_color(ch_status_t status);
lv_color_t hmi_daq_status_color(daq_status_t status);
const char *hmi_daq_status_str(daq_status_t status);

/* ============================================================
 * Lifecycle
 * ============================================================ */
esp_err_t hmi_init(void);
esp_err_t hmi_start(void);   /* Blocks until shutdown */

#ifdef __cplusplus
}
#endif
