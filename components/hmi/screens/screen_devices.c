/**
 * @file screen_devices.c
 * @brief DAQ device management screen.
 *
 * Shows a list of registered DAQ devices with their connection status.
 * Provides a form to add new UART or MQTT devices.
 *
 * Layout (800 × 380):
 *   ┌────────────────────────────────────────────────────────────┐
 *   │  [ + Add Device ]                                          │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │  DAQ1  (UART0 / 115200)       ● Connected  [Remove]  │  │
 *   │  │  DAQ2  (MQTT / broker:1883)   ○ Error      [Remove]  │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   │  ┌─ Add Device form (shown on "+ Add Device" tap) ───────┐ │
 *   │  │  Name: ___________  Type: [UART ▼]                    │ │
 *   │  │  Port: [0 ▼]  Baud: [115200 ▼]                       │ │
 *   │  │  [Cancel]                          [Add]              │ │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   └────────────────────────────────────────────────────────────┘
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lvgl.h"
#include "hmi.h"
#include "daq_manager.h"

static lv_obj_t *s_panel      = NULL;
static lv_obj_t *s_list       = NULL;
static lv_obj_t *s_add_form   = NULL;
static bool      s_form_open  = false;

/* Form widgets */
static lv_obj_t *s_ta_name    = NULL;
static lv_obj_t *s_dd_type    = NULL;
static lv_obj_t *s_dd_port    = NULL;
static lv_obj_t *s_dd_baud    = NULL;
static lv_obj_t *s_ta_host    = NULL;
static lv_obj_t *s_ta_topic   = NULL;
static lv_obj_t *s_row_uart   = NULL;
static lv_obj_t *s_row_mqtt   = NULL;

/* ============================================================
 * Remove button callback
 * ============================================================ */
static void remove_btn_cb(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    daq_manager_remove_device(idx);
    daq_manager_save_config();
    screen_devices_refresh();
}

/* ============================================================
 * Rebuild the device list
 * ============================================================ */
static void rebuild_list(void)
{
    lv_obj_clean(s_list);

    daq_manager_lock();
    uint8_t cnt = daq_manager_get_device_count();
    for (uint8_t i = 0; i < cnt; i++) {
        const daq_device_t *dev = daq_manager_get_device(i);
        if (!dev) continue;

        /* List row container */
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 50);
        hmi_style_card(row);
        lv_obj_set_style_pad_hor(row, 12, 0);
        lv_obj_set_style_pad_ver(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Device name */
        lv_obj_t *lbl_name = lv_label_create(row);
        char desc[64];
        if (dev->conn_type == DAQ_CONN_UART) {
            snprintf(desc, sizeof(desc), "%s  (UART%d, %d baud)",
                     dev->name[0] ? dev->name : dev->id,
                     dev->conn.uart.port_num, dev->conn.uart.baud_rate);
        } else if (dev->conn_type == DAQ_CONN_WIFI_MQTT) {
            snprintf(desc, sizeof(desc), "%s  (MQTT: %s)",
                     dev->name[0] ? dev->name : dev->id,
                     dev->conn.mqtt.broker_uri);
        } else {
            snprintf(desc, sizeof(desc), "%s  (TCP: %s:%d)",
                     dev->name[0] ? dev->name : dev->id,
                     dev->conn.tcp.host, dev->conn.tcp.port);
        }
        lv_label_set_text(lbl_name, desc);
        lv_obj_set_style_text_color(lbl_name, HMI_COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 0, 0);

        /* Status badge */
        lv_obj_t *lbl_status = lv_label_create(row);
        lv_label_set_text(lbl_status, hmi_daq_status_str(dev->status));
        lv_obj_set_style_text_color(lbl_status, hmi_daq_status_color(dev->status), 0);
        lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_status, LV_ALIGN_RIGHT_MID, -80, 0);

        /* Remove button */
        lv_obj_t *btn_remove = lv_button_create(row);
        lv_obj_set_size(btn_remove, 70, 30);
        lv_obj_align(btn_remove, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(btn_remove, HMI_COL_ALARM, 0);
        lv_obj_set_style_bg_opa(btn_remove, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn_remove, 6, 0);
        lv_obj_set_style_border_width(btn_remove, 0, 0);
        lv_obj_t *lbl_rm = lv_label_create(btn_remove);
        lv_label_set_text(lbl_rm, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(lbl_rm, HMI_COL_TEXT, 0);
        lv_obj_center(lbl_rm);
        lv_obj_add_event_cb(btn_remove, remove_btn_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
    daq_manager_unlock();

    if (cnt == 0) {
        lv_obj_t *lbl = lv_label_create(s_list);
        lv_label_set_text(lbl, "No devices added yet.");
        lv_obj_set_style_text_color(lbl, HMI_COL_TEXT_MUTED, 0);
        lv_obj_center(lbl);
    }
}

/* ============================================================
 * Form type-selector → show/hide UART vs MQTT rows
 * ============================================================ */
static void type_dd_cb(lv_event_t *e)
{
    uint16_t sel = lv_dropdown_get_selected(s_dd_type);
    /* 0 = UART, 1 = MQTT */
    if (sel == 0) {
        lv_obj_clear_flag(s_row_uart, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_row_mqtt, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_row_uart, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_row_mqtt, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ============================================================
 * Form: Add button callback
 * ============================================================ */
static void add_confirm_cb(lv_event_t *e)
{
    daq_device_t dev = {0};

    const char *name = lv_textarea_get_text(s_ta_name);
    strncpy(dev.name, name, DAQ_NAME_LEN - 1);
    snprintf(dev.id,  DAQ_ID_LEN, "DAQ%02d", daq_manager_get_device_count());
    dev.enabled            = true;
    dev.poll_interval_ms   = 1000;

    uint16_t type_sel = lv_dropdown_get_selected(s_dd_type);
    if (type_sel == 0) {
        /* UART */
        dev.conn_type = DAQ_CONN_UART;
        dev.conn.uart.port_num  = (int)lv_dropdown_get_selected(s_dd_port);
        static const int bauds[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800};
        int baud_sel = (int)lv_dropdown_get_selected(s_dd_baud);
        if (baud_sel < 7) {
            dev.conn.uart.baud_rate = bauds[baud_sel];
        } else {
            dev.conn.uart.baud_rate = 115200;
        }
    } else {
        /* MQTT */
        dev.conn_type = DAQ_CONN_WIFI_MQTT;
        strncpy(dev.conn.mqtt.broker_uri, lv_textarea_get_text(s_ta_host),
                sizeof(dev.conn.mqtt.broker_uri) - 1);
        strncpy(dev.conn.mqtt.topic, lv_textarea_get_text(s_ta_topic),
                sizeof(dev.conn.mqtt.topic) - 1);
        strncpy(dev.conn.mqtt.client_id, dev.id, DAQ_ID_LEN - 1);
    }

    /* Initialise thresholds to NaN */
    for (int c = 0; c < DAQ_MAX_CHANNELS; c++) {
        dev.channels[c].warn_lo  = NAN;
        dev.channels[c].warn_hi  = NAN;
        dev.channels[c].alarm_lo = NAN;
        dev.channels[c].alarm_hi = NAN;
        dev.channels[c].value_min = NAN;
        dev.channels[c].value_max = NAN;
    }

    daq_manager_add_device(&dev, NULL);
    daq_manager_save_config();

    /* Collapse form, refresh list */
    lv_obj_add_flag(s_add_form, LV_OBJ_FLAG_HIDDEN);
    s_form_open = false;
    rebuild_list();
}

static void add_cancel_cb(lv_event_t *e)
{
    lv_obj_add_flag(s_add_form, LV_OBJ_FLAG_HIDDEN);
    s_form_open = false;
}

static void add_btn_cb(lv_event_t *e)
{
    if (!s_form_open) {
        lv_obj_clear_flag(s_add_form, LV_OBJ_FLAG_HIDDEN);
        s_form_open = true;
    }
}

/* ============================================================
 * Build add-device form
 * ============================================================ */
static void build_add_form(lv_obj_t *parent)
{
    s_add_form = lv_obj_create(parent);
    lv_obj_set_size(s_add_form, HMI_DISPLAY_W - 24, 200);
    hmi_style_card(s_add_form);
    lv_obj_set_style_pad_all(s_add_form, 10, 0);
    lv_obj_add_flag(s_add_form, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_add_form, LV_OBJ_FLAG_SCROLLABLE);

    /* Row 1: name + type */
    lv_obj_t *r1 = lv_obj_create(s_add_form);
    lv_obj_set_size(r1, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(r1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r1, 0, 0);
    lv_obj_set_style_pad_all(r1, 0, 0);
    lv_obj_clear_flag(r1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_name = lv_label_create(r1);
    lv_label_set_text(lbl_name, "Name:");
    hmi_style_label_muted(lbl_name);
    lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 0, 0);

    s_ta_name = lv_textarea_create(r1);
    lv_obj_set_size(s_ta_name, 200, 32);
    lv_obj_align(s_ta_name, LV_ALIGN_LEFT_MID, 50, 0);
    lv_textarea_set_one_line(s_ta_name, true);
    lv_textarea_set_placeholder_text(s_ta_name, "My DAQ");
    lv_obj_set_style_text_font(s_ta_name, &lv_font_montserrat_12, 0);

    lv_obj_t *lbl_type = lv_label_create(r1);
    lv_label_set_text(lbl_type, "Type:");
    hmi_style_label_muted(lbl_type);
    lv_obj_align(lbl_type, LV_ALIGN_LEFT_MID, 270, 0);

    s_dd_type = lv_dropdown_create(r1);
    lv_dropdown_set_options(s_dd_type, "UART\nMQTT");
    lv_obj_set_size(s_dd_type, 120, 32);
    lv_obj_align(s_dd_type, LV_ALIGN_LEFT_MID, 310, 0);
    lv_obj_set_style_text_font(s_dd_type, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_dd_type, type_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Row 2: UART params */
    s_row_uart = lv_obj_create(s_add_form);
    lv_obj_set_size(s_row_uart, LV_PCT(100), 40);
    lv_obj_set_pos(s_row_uart, 0, 50);
    lv_obj_set_style_bg_opa(s_row_uart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_row_uart, 0, 0);
    lv_obj_set_style_pad_all(s_row_uart, 0, 0);
    lv_obj_clear_flag(s_row_uart, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_port = lv_label_create(s_row_uart);
    lv_label_set_text(lbl_port, "Port:");
    hmi_style_label_muted(lbl_port);
    lv_obj_align(lbl_port, LV_ALIGN_LEFT_MID, 0, 0);

    s_dd_port = lv_dropdown_create(s_row_uart);
    lv_dropdown_set_options(s_dd_port, "UART0\nUART1\nUART2");
    lv_obj_set_size(s_dd_port, 110, 32);
    lv_obj_align(s_dd_port, LV_ALIGN_LEFT_MID, 42, 0);
    lv_obj_set_style_text_font(s_dd_port, &lv_font_montserrat_12, 0);

    lv_obj_t *lbl_baud = lv_label_create(s_row_uart);
    lv_label_set_text(lbl_baud, "Baud:");
    hmi_style_label_muted(lbl_baud);
    lv_obj_align(lbl_baud, LV_ALIGN_LEFT_MID, 166, 0);

    s_dd_baud = lv_dropdown_create(s_row_uart);
    lv_dropdown_set_options(s_dd_baud,
        "9600\n19200\n38400\n57600\n115200\n230400\n460800");
    lv_dropdown_set_selected(s_dd_baud, 4);   /* default 115200 */
    lv_obj_set_size(s_dd_baud, 120, 32);
    lv_obj_align(s_dd_baud, LV_ALIGN_LEFT_MID, 210, 0);
    lv_obj_set_style_text_font(s_dd_baud, &lv_font_montserrat_12, 0);

    /* Row 3: MQTT params */
    s_row_mqtt = lv_obj_create(s_add_form);
    lv_obj_set_size(s_row_mqtt, LV_PCT(100), 80);
    lv_obj_set_pos(s_row_mqtt, 0, 50);
    lv_obj_set_style_bg_opa(s_row_mqtt, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_row_mqtt, 0, 0);
    lv_obj_set_style_pad_all(s_row_mqtt, 0, 0);
    lv_obj_clear_flag(s_row_mqtt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_row_mqtt, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *lbl_host = lv_label_create(s_row_mqtt);
    lv_label_set_text(lbl_host, "Broker URI:");
    hmi_style_label_muted(lbl_host);
    lv_obj_set_pos(lbl_host, 0, 4);

    s_ta_host = lv_textarea_create(s_row_mqtt);
    lv_obj_set_size(s_ta_host, 400, 30);
    lv_obj_set_pos(s_ta_host, 90, 0);
    lv_textarea_set_one_line(s_ta_host, true);
    lv_textarea_set_placeholder_text(s_ta_host, "mqtt://192.168.1.10:1883");
    lv_obj_set_style_text_font(s_ta_host, &lv_font_montserrat_12, 0);

    lv_obj_t *lbl_topic = lv_label_create(s_row_mqtt);
    lv_label_set_text(lbl_topic, "Topic:");
    hmi_style_label_muted(lbl_topic);
    lv_obj_set_pos(lbl_topic, 0, 44);

    s_ta_topic = lv_textarea_create(s_row_mqtt);
    lv_obj_set_size(s_ta_topic, 400, 30);
    lv_obj_set_pos(s_ta_topic, 90, 40);
    lv_textarea_set_one_line(s_ta_topic, true);
    lv_textarea_set_placeholder_text(s_ta_topic, "daq/device1/data");
    lv_obj_set_style_text_font(s_ta_topic, &lv_font_montserrat_12, 0);

    /* Action row */
    lv_obj_t *r_act = lv_obj_create(s_add_form);
    lv_obj_set_size(r_act, LV_PCT(100), 36);
    lv_obj_set_pos(r_act, 0, 155);
    lv_obj_set_style_bg_opa(r_act, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r_act, 0, 0);
    lv_obj_set_style_pad_all(r_act, 0, 0);
    lv_obj_clear_flag(r_act, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_cancel = lv_button_create(r_act);
    lv_obj_set_size(btn_cancel, 100, 30);
    lv_obj_align(btn_cancel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel, HMI_COL_SURFACE2, 0);
    lv_obj_set_style_bg_opa(btn_cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_border_color(btn_cancel, HMI_COL_BORDER, 0);
    lv_obj_set_style_radius(btn_cancel, 6, 0);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_set_style_text_color(lbl_cancel, HMI_COL_TEXT, 0);
    lv_obj_center(lbl_cancel);
    lv_obj_add_event_cb(btn_cancel, add_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_add = lv_button_create(r_act);
    lv_obj_set_size(btn_add, 100, 30);
    lv_obj_align(btn_add, LV_ALIGN_RIGHT_MID, 0, 0);
    hmi_style_btn_primary(btn_add);
    lv_obj_t *lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, LV_SYMBOL_OK "  Add");
    lv_obj_set_style_text_color(lbl_add, HMI_COL_TEXT, 0);
    lv_obj_center(lbl_add);
    lv_obj_add_event_cb(btn_add, add_confirm_cb, LV_EVENT_CLICKED, NULL);
}

/* ============================================================
 * Public: create panel
 * ============================================================ */
lv_obj_t *screen_devices_create(lv_obj_t *parent)
{
    s_panel = lv_obj_create(parent);
    lv_obj_set_style_bg_color(s_panel, HMI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 12, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Header: title + "Add" button */
    lv_obj_t *header = lv_obj_create(s_panel);
    lv_obj_set_size(header, HMI_DISPLAY_W - 24, 36);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_hdr = lv_label_create(header);
    lv_label_set_text(lbl_hdr, "DAQ Devices");
    lv_obj_set_style_text_color(lbl_hdr, HMI_COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_hdr, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_hdr, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *btn_add = lv_button_create(header);
    lv_obj_set_size(btn_add, 120, 30);
    lv_obj_align(btn_add, LV_ALIGN_RIGHT_MID, 0, 0);
    hmi_style_btn_primary(btn_add);
    lv_obj_t *lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, LV_SYMBOL_PLUS "  Add Device");
    lv_obj_set_style_text_color(lbl_add, HMI_COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_add, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_add);
    lv_obj_add_event_cb(btn_add, add_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Device list (scrollable) */
    s_list = lv_obj_create(s_panel);
    lv_obj_set_size(s_list, HMI_DISPLAY_W - 24, 170);
    lv_obj_set_pos(s_list, 0, 44);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_gap(s_list, 6, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_layout(s_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);

    /* Add-device form (below the list) */
    build_add_form(s_panel);
    lv_obj_set_pos(s_add_form, 0, 222);

    rebuild_list();
    return s_panel;
}

/* ============================================================
 * Public: refresh status badges (called 10 Hz)
 * ============================================================ */
void screen_devices_refresh(void)
{
    rebuild_list();   /* Lightweight enough at 10 Hz */
}
