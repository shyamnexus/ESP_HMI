#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "bsp.h"
#include "hmi.h"
#include "daq_manager.h"

static const char *TAG = "HMI";

/* ============================================================
 * LVGL display & touch handles
 * ============================================================ */
static lv_display_t *g_disp  = NULL;
static lv_indev_t   *g_indev = NULL;

/* ============================================================
 * Root layout objects
 * ============================================================ */
static lv_obj_t *g_root_screen  = NULL;
static lv_obj_t *g_topbar       = NULL;
static lv_obj_t *g_content_area = NULL;
static lv_obj_t *g_navbar       = NULL;

/* Panel containers */
static lv_obj_t *g_panels[HMI_PANEL_COUNT] = {NULL};

/* Navigation buttons */
static lv_obj_t *g_nav_btns[3] = {NULL};

/* Status bar labels */
static lv_obj_t *g_lbl_time   = NULL;
static lv_obj_t *g_lbl_wifi   = NULL;
static lv_obj_t *g_lbl_title  = NULL;

static hmi_panel_t g_active_panel = HMI_PANEL_DASHBOARD;

/* UI refresh timer (100 ms period = 10 Hz) */
static lv_timer_t *g_refresh_timer = NULL;

/* ============================================================
 * Shared style helpers
 * ============================================================ */
void hmi_style_card(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, HMI_COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, HMI_COL_BORDER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, HMI_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(obj, HMI_CARD_PAD, 0);
}

void hmi_style_btn_primary(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, HMI_COL_PRIMARY, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_text_color(btn, HMI_COL_TEXT, 0);
}

void hmi_style_label_title(lv_obj_t *label)
{
    lv_obj_set_style_text_color(label, HMI_COL_TEXT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
}

void hmi_style_label_value(lv_obj_t *label)
{
    lv_obj_set_style_text_color(label, HMI_COL_TEXT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
}

void hmi_style_label_unit(lv_obj_t *label)
{
    lv_obj_set_style_text_color(label, HMI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
}

void hmi_style_label_muted(lv_obj_t *label)
{
    lv_obj_set_style_text_color(label, HMI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
}

lv_color_t hmi_status_color(ch_status_t status)
{
    switch (status) {
        case CH_STATUS_WARNING: return HMI_COL_WARNING;
        case CH_STATUS_ALARM:   return HMI_COL_ALARM;
        case CH_STATUS_STALE:   return HMI_COL_TEXT_MUTED;
        default:                return HMI_COL_SUCCESS;
    }
}

lv_color_t hmi_daq_status_color(daq_status_t status)
{
    switch (status) {
        case DAQ_STATUS_CONNECTED:   return HMI_COL_SUCCESS;
        case DAQ_STATUS_CONNECTING:  return HMI_COL_WARNING;
        case DAQ_STATUS_ERROR:       return HMI_COL_ALARM;
        default:                     return HMI_COL_TEXT_MUTED;
    }
}

const char *hmi_daq_status_str(daq_status_t status)
{
    switch (status) {
        case DAQ_STATUS_CONNECTED:    return "Connected";
        case DAQ_STATUS_CONNECTING:   return "Connecting...";
        case DAQ_STATUS_ERROR:        return "Error";
        default:                      return "Disconnected";
    }
}

/* ============================================================
 * Navigation
 * ============================================================ */
void hmi_navigate(hmi_panel_t panel)
{
    if (panel >= HMI_PANEL_COUNT) return;

    /* Hide all panels */
    for (int i = 0; i < HMI_PANEL_COUNT; i++) {
        if (g_panels[i]) {
            lv_obj_add_flag(g_panels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Show requested panel */
    if (g_panels[panel]) {
        lv_obj_clear_flag(g_panels[panel], LV_OBJ_FLAG_HIDDEN);
    }

    /* Update nav button styles */
    const char *titles[] = {"Dashboard", "Devices", "Settings"};
    for (int i = 0; i < 3; i++) {
        if (!g_nav_btns[i]) continue;
        bool active = ((int)panel == i);
        lv_obj_set_style_text_color(g_nav_btns[i],
            active ? HMI_COL_NAV_ACTIVE : HMI_COL_TEXT_MUTED, 0);
        lv_obj_set_style_border_color(g_nav_btns[i],
            active ? HMI_COL_NAV_ACTIVE : HMI_COL_BORDER, 0);
        lv_obj_set_style_border_side(g_nav_btns[i],
            active ? LV_BORDER_SIDE_TOP : LV_BORDER_SIDE_NONE, 0);
    }

    /* Update title */
    if (panel < 3 && g_lbl_title) {
        lv_label_set_text(g_lbl_title, titles[panel]);
    }

    g_active_panel = panel;
}

static void nav_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    hmi_navigate((hmi_panel_t)idx);
}

/* ============================================================
 * Top status bar builder
 * ============================================================ */
static void build_topbar(void)
{
    g_topbar = lv_obj_create(g_root_screen);
    lv_obj_set_size(g_topbar, HMI_DISPLAY_W, HMI_TOPBAR_H);
    lv_obj_align(g_topbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(g_topbar, HMI_COL_NAV_BG, 0);
    lv_obj_set_style_bg_opa(g_topbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_topbar, 0, 0);
    lv_obj_set_style_border_side(g_topbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(g_topbar, HMI_COL_BORDER, 0);
    lv_obj_set_style_pad_hor(g_topbar, 12, 0);
    lv_obj_set_style_pad_ver(g_topbar, 0, 0);
    lv_obj_clear_flag(g_topbar, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    g_lbl_title = lv_label_create(g_topbar);
    lv_label_set_text(g_lbl_title, "Dashboard");
    lv_obj_set_style_text_color(g_lbl_title, HMI_COL_TEXT, 0);
    lv_obj_set_style_text_font(g_lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_align(g_lbl_title, LV_ALIGN_LEFT_MID, 0, 0);

    /* Wi-Fi status */
    g_lbl_wifi = lv_label_create(g_topbar);
    lv_label_set_text(g_lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(g_lbl_wifi, HMI_COL_TEXT_MUTED, 0);
    lv_obj_align(g_lbl_wifi, LV_ALIGN_RIGHT_MID, -50, 0);

    /* Clock */
    g_lbl_time = lv_label_create(g_topbar);
    lv_label_set_text(g_lbl_time, "--:--");
    lv_obj_set_style_text_color(g_lbl_time, HMI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(g_lbl_time, &lv_font_montserrat_12, 0);
    lv_obj_align(g_lbl_time, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* ============================================================
 * Bottom navigation bar builder
 * ============================================================ */
static void build_navbar(void)
{
    g_navbar = lv_obj_create(g_root_screen);
    lv_obj_set_size(g_navbar, HMI_DISPLAY_W, HMI_NAVBAR_H);
    lv_obj_align(g_navbar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(g_navbar, HMI_COL_NAV_BG, 0);
    lv_obj_set_style_bg_opa(g_navbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_navbar, 1, 0);
    lv_obj_set_style_border_side(g_navbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(g_navbar, HMI_COL_BORDER, 0);
    lv_obj_set_style_pad_all(g_navbar, 0, 0);
    lv_obj_clear_flag(g_navbar, LV_OBJ_FLAG_SCROLLABLE);

    /* Flex row layout */
    lv_obj_set_layout(g_navbar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_navbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_navbar, LV_FLEX_ALIGN_SPACE_EVENLY,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static const char *nav_labels[] = {
        LV_SYMBOL_HOME    "\nDashboard",
        LV_SYMBOL_LIST    "\nDevices",
        LV_SYMBOL_SETTINGS "\nSettings",
    };

    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_button_create(g_navbar);
        lv_obj_set_size(btn, HMI_DISPLAY_W / 3, HMI_NAVBAR_H);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_NONE, 0);
        lv_obj_set_style_border_color(btn, HMI_COL_TEXT_MUTED, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 4, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, nav_labels[i]);
        lv_obj_set_style_text_color(lbl, HMI_COL_TEXT_MUTED, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, nav_btn_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        g_nav_btns[i] = lbl;   /* Store label so we can change colour */
    }
}

/* ============================================================
 * Periodic UI refresh callback (10 Hz)
 * ============================================================ */
static void refresh_timer_cb(lv_timer_t *t)
{
    /* Update Wi-Fi indicator */
    if (g_lbl_wifi) {
        bool connected = daq_wifi_is_connected();
        lv_obj_set_style_text_color(g_lbl_wifi,
            connected ? HMI_COL_SUCCESS : HMI_COL_TEXT_MUTED, 0);
    }

    /* Delegate per-screen refresh */
    switch (g_active_panel) {
        case HMI_PANEL_DASHBOARD: screen_dashboard_refresh(); break;
        case HMI_PANEL_DEVICES:   screen_devices_refresh();   break;
        default: break;
    }
}

/* ============================================================
 * Lifecycle
 * ============================================================ */
esp_err_t hmi_init(void)
{
    const bsp_handles_t *bsp = bsp_get_handles();
    if (!bsp->lcd_panel || !bsp->touch) {
        ESP_LOGE(TAG, "BSP not initialised before HMI");
        return ESP_ERR_INVALID_STATE;
    }

    /* --- Initialise LVGL port --- */
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority  = 4;
    port_cfg.task_stack = 8192;
    port_cfg.task_affinity  = 1;   /* Core 1 */
    port_cfg.timer_period_ms = 5;  /* 5 ms LVGL tick */
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    /* --- Add RGB display ---
     * lvgl_port_add_disp() is for SPI/I2C panels only and asserts io_handle != NULL.
     * RGB panels must use lvgl_port_add_disp_rgb() with an rgb_cfg:
     *   avoid_tearing = true  → library fetches the panel's own PSRAM framebuffers
     *                           via esp_lcd_rgb_panel_get_frame_buffer() and gives
     *                           them to LVGL as direct-mode draw buffers (no extra
     *                           allocation, no copy).
     *   bb_mode       = true  → sync LVGL via on_bounce_frame_finish callback,
     *                           which matches the bounce_buffer_size_px set in the
     *                           RGB panel config (fires once per complete frame). */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = NULL,           /* Not used by RGB panels             */
        .panel_handle = bsp->lcd_panel,
        .hres         = HMI_DISPLAY_W,
        .vres         = HMI_DISPLAY_H,
        .buffer_size  = HMI_DISPLAY_W * HMI_DISPLAY_H,
        .double_buffer = true,
        .monochrome   = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .direct_mode  = true,
            .full_refresh = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode       = true,  /* bounce-buffer panel → use frame-finish CB */
            .avoid_tearing = true,  /* use panel PSRAM fbs as LVGL draw buffers  */
        },
    };
    g_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (!g_disp) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return ESP_FAIL;
    }

    /* --- Add touch input --- */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = g_disp,
        .handle = bsp->touch,
    };
    g_indev = lvgl_port_add_touch(&touch_cfg);
    if (!g_indev) {
        ESP_LOGE(TAG, "Failed to add LVGL touch");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL display %dx%d ready", HMI_DISPLAY_W, HMI_DISPLAY_H);
    return ESP_OK;
}

esp_err_t hmi_start(void)
{
    /* All LVGL calls after init must be wrapped in lock/unlock */
    lvgl_port_lock(0);

    /* Root screen */
    g_root_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_root_screen, HMI_DISPLAY_W, HMI_DISPLAY_H);
    lv_obj_set_style_bg_color(g_root_screen, HMI_COL_BG, 0);
    lv_obj_set_style_bg_opa(g_root_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_root_screen, 0, 0);
    lv_obj_clear_flag(g_root_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_topbar();
    build_navbar();

    /* Content area (between topbar and navbar) */
    g_content_area = lv_obj_create(g_root_screen);
    lv_obj_set_size(g_content_area, HMI_DISPLAY_W, HMI_CONTENT_H);
    lv_obj_set_pos(g_content_area, 0, HMI_TOPBAR_H);
    lv_obj_set_style_bg_color(g_content_area, HMI_COL_BG, 0);
    lv_obj_set_style_bg_opa(g_content_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_content_area, 0, 0);
    lv_obj_set_style_pad_all(g_content_area, 0, 0);
    lv_obj_clear_flag(g_content_area, LV_OBJ_FLAG_SCROLLABLE);

    /* Build all panel UIs */
    g_panels[HMI_PANEL_DASHBOARD] = screen_dashboard_create(g_content_area);
    g_panels[HMI_PANEL_DEVICES]   = screen_devices_create(g_content_area);
    g_panels[HMI_PANEL_SETTINGS]  = screen_settings_create(g_content_area);
    g_panels[HMI_PANEL_CHANNEL]   = screen_channel_create(g_content_area);

    /* Make all full-size within content area */
    for (int i = 0; i < HMI_PANEL_COUNT; i++) {
        if (g_panels[i]) {
            lv_obj_set_size(g_panels[i], HMI_DISPLAY_W, HMI_CONTENT_H);
            lv_obj_set_pos(g_panels[i], 0, 0);
            lv_obj_set_style_bg_color(g_panels[i], HMI_COL_BG, 0);
            lv_obj_set_style_bg_opa(g_panels[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(g_panels[i], 0, 0);
            lv_obj_set_style_pad_all(g_panels[i], 0, 0);
        }
    }

    /* 10 Hz refresh timer */
    g_refresh_timer = lv_timer_create(refresh_timer_cb, 100, NULL);

    /* Navigate to dashboard */
    hmi_navigate(HMI_PANEL_DASHBOARD);

    /* Load the screen */
    lv_scr_load(g_root_screen);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "HMI running");

    /* Keep the calling task alive; LVGL runs on its own task */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    return ESP_OK;
}
