/**
 * @file screen_settings.c
 * @brief System settings: Wi-Fi credentials, backlight, about info.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "hmi.h"
#include "bsp.h"
#include "daq_manager.h"

static const char *TAG = "SCREEN_SETTINGS";

#define NVS_NS_WIFI  "wifi_cfg"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"

static lv_obj_t *s_panel      = NULL;
static lv_obj_t *s_ta_ssid    = NULL;
static lv_obj_t *s_ta_pass    = NULL;
static lv_obj_t *s_lbl_status = NULL;
static lv_obj_t *s_slider_bl  = NULL;
static lv_obj_t *s_keyboard   = NULL;

/* ============================================================
 * NVS helpers
 * ============================================================ */
static void save_wifi_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_KEY_SSID, ssid);
        nvs_set_str(h, NVS_KEY_PASS, pass);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void load_wifi_creds(char *ssid, size_t ssid_len,
                             char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len);
        nvs_get_str(h, NVS_KEY_PASS, pass, &pass_len);
        nvs_close(h);
    }
}

/* ============================================================
 * Event handlers
 * ============================================================ */
static void hide_keyboard(void)
{
    if (s_keyboard && !lv_obj_has_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN)) {
        lv_keyboard_set_textarea(s_keyboard, NULL);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ta_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_keyboard, ta);
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_view(ta, LV_ANIM_ON);
}

static void kb_event_cb(lv_event_t *e)
{
    hide_keyboard();
}

typedef struct {
    char ssid[64];
    char pass[64];
} wifi_connect_args_t;

static bool s_connecting = false;

static void wifi_connect_task(void *arg)
{
    wifi_connect_args_t *args = (wifi_connect_args_t *)arg;
    ESP_LOGI(TAG, "Initiating Wi-Fi connection to: %s", args->ssid);
    esp_err_t err = daq_wifi_connect(args->ssid, args->pass, 15000);
    free(args);

    lvgl_port_lock(0);
    if (err == ESP_OK) {
        lv_label_set_text(s_lbl_status, "Connected!");
        lv_obj_set_style_text_color(s_lbl_status, HMI_COL_SUCCESS, 0);
    } else {
        lv_label_set_text(s_lbl_status, "Connection failed");
        lv_obj_set_style_text_color(s_lbl_status, HMI_COL_ALARM, 0);
    }
    lvgl_port_unlock();

    s_connecting = false;
    vTaskDelete(NULL);
}

static void wifi_connect_cb(lv_event_t *e)
{
    hide_keyboard();

    if (s_connecting) return;

    const char *ssid = lv_textarea_get_text(s_ta_ssid);
    const char *pass = lv_textarea_get_text(s_ta_pass);

    if (!ssid || strlen(ssid) == 0) {
        lv_label_set_text(s_lbl_status, "Error: SSID cannot be empty");
        lv_obj_set_style_text_color(s_lbl_status, HMI_COL_ALARM, 0);
        return;
    }

    save_wifi_creds(ssid, pass);

    wifi_connect_args_t *args = malloc(sizeof(wifi_connect_args_t));
    if (!args) {
        lv_label_set_text(s_lbl_status, "Error: out of memory");
        lv_obj_set_style_text_color(s_lbl_status, HMI_COL_ALARM, 0);
        return;
    }
    strncpy(args->ssid, ssid, sizeof(args->ssid) - 1);
    strncpy(args->pass, pass, sizeof(args->pass) - 1);

    lv_label_set_text(s_lbl_status, "Connecting…");
    lv_obj_set_style_text_color(s_lbl_status, HMI_COL_WARNING, 0);

    s_connecting = true;
    if (xTaskCreate(wifi_connect_task, "wifi_conn", 4096, args, 5, NULL) != pdPASS) {
        s_connecting = false;
        free(args);
        lv_label_set_text(s_lbl_status, "Error: task creation failed");
        lv_obj_set_style_text_color(s_lbl_status, HMI_COL_ALARM, 0);
    }
}

static void backlight_slider_cb(lv_event_t *e)
{
    int32_t val = lv_slider_get_value(s_slider_bl);
    /* val is 0-100 %. On this board, brightness is on/off via IO expander.
     * A PWM-capable GPIO can be added for true dimming; for now, treat any
     * non-zero value as "on". */
    bsp_backlight_set(val > 10);
}

static void save_all_cb(lv_event_t *e)
{
    daq_manager_save_config();
    lv_label_set_text(s_lbl_status, "All settings saved.");
    lv_obj_set_style_text_color(s_lbl_status, HMI_COL_SUCCESS, 0);
}

/* ============================================================
 * Section builder helpers
 * ============================================================ */
static lv_obj_t *make_section(lv_obj_t *parent, int y, int h, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, HMI_DISPLAY_W - 24, h);
    lv_obj_set_pos(card, 0, y);
    hmi_style_card(card);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (title) {
        lv_obj_t *lbl = lv_label_create(card);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_color(lbl, HMI_COL_PRIMARY, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    }

    return card;
}

/* ============================================================
 * Public: create panel
 * ============================================================ */
lv_obj_t *screen_settings_create(lv_obj_t *parent)
{
    s_panel = lv_obj_create(parent);
    lv_obj_set_style_bg_color(s_panel, HMI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 12, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Wi-Fi section ── */
    lv_obj_t *wifi_card = make_section(s_panel, 0, 170, LV_SYMBOL_WIFI "  Wi-Fi");

    lv_obj_t *lbl_ssid = lv_label_create(wifi_card);
    lv_label_set_text(lbl_ssid, "SSID:");
    hmi_style_label_muted(lbl_ssid);
    lv_obj_set_pos(lbl_ssid, 0, 26);

    s_ta_ssid = lv_textarea_create(wifi_card);
    lv_obj_set_size(s_ta_ssid, 350, 32);
    lv_obj_set_pos(s_ta_ssid, 60, 22);
    lv_textarea_set_one_line(s_ta_ssid, true);
    lv_textarea_set_placeholder_text(s_ta_ssid, "Network name");
    lv_obj_set_style_text_font(s_ta_ssid, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_ta_ssid, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *lbl_pass = lv_label_create(wifi_card);
    lv_label_set_text(lbl_pass, "Pass:");
    hmi_style_label_muted(lbl_pass);
    lv_obj_set_pos(lbl_pass, 0, 68);

    s_ta_pass = lv_textarea_create(wifi_card);
    lv_obj_set_size(s_ta_pass, 350, 32);
    lv_obj_set_pos(s_ta_pass, 60, 64);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_textarea_set_placeholder_text(s_ta_pass, "Password");
    lv_obj_set_style_text_font(s_ta_pass, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(s_ta_pass, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    /* Load saved creds */
    char ssid_buf[64] = {0}, pass_buf[64] = {0};
    load_wifi_creds(ssid_buf, sizeof(ssid_buf), pass_buf, sizeof(pass_buf));
    if (ssid_buf[0]) lv_textarea_set_text(s_ta_ssid, ssid_buf);

    lv_obj_t *btn_connect = lv_button_create(wifi_card);
    lv_obj_set_size(btn_connect, 120, 30);
    lv_obj_set_pos(btn_connect, 0, 106);
    hmi_style_btn_primary(btn_connect);
    lv_obj_t *lbl_conn = lv_label_create(btn_connect);
    lv_label_set_text(lbl_conn, LV_SYMBOL_WIFI "  Connect");
    lv_obj_set_style_text_color(lbl_conn, HMI_COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_conn, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_conn);
    lv_obj_add_event_cb(btn_connect, wifi_connect_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_status = lv_label_create(wifi_card);
    lv_label_set_text(s_lbl_status,
                      daq_wifi_is_connected() ? "Connected" : "Not connected");
    lv_obj_set_style_text_color(s_lbl_status,
                                daq_wifi_is_connected() ? HMI_COL_SUCCESS : HMI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(s_lbl_status, 140, 112);

    /* ── Display / backlight section ── */
    lv_obj_t *bl_card = make_section(s_panel, 178, 80, LV_SYMBOL_IMAGE "  Display");

    lv_obj_t *lbl_bl = lv_label_create(bl_card);
    lv_label_set_text(lbl_bl, "Brightness:");
    hmi_style_label_muted(lbl_bl);
    lv_obj_set_pos(lbl_bl, 0, 26);

    s_slider_bl = lv_slider_create(bl_card);
    lv_obj_set_size(s_slider_bl, 400, 12);
    lv_obj_set_pos(s_slider_bl, 110, 30);
    lv_slider_set_range(s_slider_bl, 0, 100);
    lv_slider_set_value(s_slider_bl, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_bl, HMI_COL_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_bl, HMI_COL_PRIMARY, LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider_bl, backlight_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── About section ── */
    lv_obj_t *about_card = make_section(s_panel, 266, 76, NULL);

    lv_obj_t *lbl_about = lv_label_create(about_card);
    lv_label_set_text(lbl_about,
                      "ESP HMI  v1.0.0\n"
                      "Board: Waveshare ESP32-S3-Touch-LCD-4.3B\n"
                      "Display: 800×480 RGB  |  Touch: GT911  |  PSRAM: 8 MB");
    lv_obj_set_style_text_color(lbl_about, HMI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl_about, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_about, LV_ALIGN_LEFT_MID, 0, 0);

    /* ── Save all button ── */
    lv_obj_t *btn_save = lv_button_create(s_panel);
    lv_obj_set_size(btn_save, 140, 34);
    lv_obj_set_pos(btn_save, HMI_DISPLAY_W - 24 - 140, 346);
    hmi_style_btn_primary(btn_save);
    lv_obj_t *lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, LV_SYMBOL_SAVE "  Save All");
    lv_obj_set_style_text_color(lbl_save, HMI_COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_save, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_save);
    lv_obj_add_event_cb(btn_save, save_all_cb, LV_EVENT_CLICKED, NULL);

    /* ── On-screen keyboard (hidden until a textarea is focused) ── */
    s_keyboard = lv_keyboard_create(parent);
    lv_obj_set_size(s_keyboard, HMI_DISPLAY_W, 200);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, kb_event_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_keyboard, kb_event_cb, LV_EVENT_CANCEL, NULL);

    return s_panel;
}
