/**
 * @file screen_channel.c
 * @brief Full-screen channel detail view with LVGL line chart.
 *
 * Layout:
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │ [←] Channel Name                          Status: OK               │
 *   │ ─────────────────────────────────────────────────────────────────── │
 *   │  Current:  3.14 V        Min: 2.10    Max: 3.80                    │
 *   │ ─────────────────────────────────────────────────────────────────── │
 *   │                                                                     │
 *   │            ╔══════════════════════════════╗                         │
 *   │            ║       Line chart (2 min)     ║                         │
 *   │            ╚══════════════════════════════╝                         │
 *   │                                                                     │
 *   │  Warn Lo: _____    Warn Hi: _____   [Apply Thresholds]             │
 *   └─────────────────────────────────────────────────────────────────────┘
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "lvgl.h"
#include "hmi.h"
#include "daq_manager.h"

static lv_obj_t *s_panel        = NULL;
static lv_obj_t *s_lbl_name     = NULL;
static lv_obj_t *s_lbl_status   = NULL;
static lv_obj_t *s_lbl_value    = NULL;
static lv_obj_t *s_lbl_unit     = NULL;
static lv_obj_t *s_lbl_min      = NULL;
static lv_obj_t *s_lbl_max      = NULL;
static lv_obj_t *s_chart        = NULL;
static lv_chart_series_t *s_ser = NULL;

static uint8_t s_dev_idx = 0;
static uint8_t s_ch_idx  = 0;

#define CHART_H     200
#define CHART_W     (HMI_DISPLAY_W - 32)

/* Chart Y range with some headroom */
static float s_y_min = 0.0f;
static float s_y_max = 100.0f;

/* ============================================================
 * Back button
 * ============================================================ */
static void back_btn_cb(lv_event_t *e)
{
    hmi_navigate(HMI_PANEL_DASHBOARD);
}

/* ============================================================
 * Build panel (once)
 * ============================================================ */
lv_obj_t *screen_channel_create(lv_obj_t *parent)
{
    s_panel = lv_obj_create(parent);
    lv_obj_set_style_bg_color(s_panel, HMI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 12, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Row 1: back button + channel name + status ── */
    lv_obj_t *row1 = lv_obj_create(s_panel);
    lv_obj_set_size(row1, HMI_DISPLAY_W - 24, 36);
    lv_obj_set_pos(row1, 0, 0);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_style_pad_all(row1, 0, 0);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_button_create(row1);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    hmi_style_btn_primary(btn_back);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, back_btn_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_name = lv_label_create(row1);
    lv_label_set_text(s_lbl_name, "Channel");
    lv_obj_set_style_text_color(s_lbl_name, HMI_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_lbl_name, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_name, LV_ALIGN_CENTER, 0, 0);

    s_lbl_status = lv_label_create(row1);
    lv_label_set_text(s_lbl_status, "Normal");
    lv_obj_set_style_text_color(s_lbl_status, HMI_COL_SUCCESS, 0);
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_RIGHT_MID, 0, 0);

    /* ── Row 2: current value + unit + min/max ── */
    lv_obj_t *row2 = lv_obj_create(s_panel);
    lv_obj_set_size(row2, HMI_DISPLAY_W - 24, 56);
    lv_obj_set_pos(row2, 0, 44);
    lv_obj_set_style_bg_color(row2, HMI_COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(row2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_style_radius(row2, HMI_CARD_RADIUS, 0);
    lv_obj_set_style_pad_hor(row2, 16, 0);
    lv_obj_set_style_pad_ver(row2, 8, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_value = lv_label_create(row2);
    lv_label_set_text(s_lbl_value, "---");
    lv_obj_set_style_text_color(s_lbl_value, HMI_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_lbl_value, &lv_font_montserrat_28, 0);
    lv_obj_align(s_lbl_value, LV_ALIGN_LEFT_MID, 0, 0);

    s_lbl_unit = lv_label_create(row2);
    lv_label_set_text(s_lbl_unit, "");
    hmi_style_label_unit(s_lbl_unit);
    lv_obj_align_to(s_lbl_unit, s_lbl_value, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, 0);

    s_lbl_min = lv_label_create(row2);
    lv_label_set_text(s_lbl_min, "Min: ---");
    hmi_style_label_muted(s_lbl_min);
    lv_obj_align(s_lbl_min, LV_ALIGN_RIGHT_MID, -100, 0);

    s_lbl_max = lv_label_create(row2);
    lv_label_set_text(s_lbl_max, "Max: ---");
    hmi_style_label_muted(s_lbl_max);
    lv_obj_align(s_lbl_max, LV_ALIGN_RIGHT_MID, 0, 0);

    /* ── Chart ── */
    s_chart = lv_chart_create(s_panel);
    lv_obj_set_size(s_chart, CHART_W, CHART_H);
    lv_obj_set_pos(s_chart, 0, 112);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, DAQ_HISTORY_DEPTH);
    lv_chart_set_div_line_count(s_chart, 5, 5);
    lv_obj_set_style_bg_color(s_chart, HMI_COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_chart, HMI_COL_BORDER, 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_radius(s_chart, HMI_CARD_RADIUS, 0);
    lv_obj_set_style_line_color(s_chart, HMI_COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart, 1, LV_PART_MAIN);

    s_ser = lv_chart_add_series(s_chart, HMI_COL_PRIMARY, LV_CHART_AXIS_PRIMARY_Y);

    /* Axis label callbacks are optional; skip for compactness */

    return s_panel;
}

/* ============================================================
 * Public: populate chart and labels for a specific channel
 * ============================================================ */
void screen_channel_show(uint8_t dev_idx, uint8_t ch_idx)
{
    s_dev_idx = dev_idx;
    s_ch_idx  = ch_idx;

    daq_manager_lock();
    const daq_device_t *dev = daq_manager_get_device(dev_idx);
    if (!dev || ch_idx >= dev->num_channels) {
        daq_manager_unlock();
        return;
    }
    const daq_channel_t *ch = &dev->channels[ch_idx];

    /* Title */
    char title[CH_NAME_LEN + DAQ_NAME_LEN + 4];
    snprintf(title, sizeof(title), "%s / %s",
             dev->name[0] ? dev->name : dev->id, ch->name);
    lv_label_set_text(s_lbl_name, title);

    /* Status */
    static const char *status_str[] = { "Normal", "Warning", "Alarm", "Stale" };
    lv_label_set_text(s_lbl_status, status_str[ch->status]);
    lv_obj_set_style_text_color(s_lbl_status, hmi_status_color(ch->status), 0);

    /* Current value */
    lv_label_set_text_fmt(s_lbl_value, "%.4g", (double)ch->value);
    lv_label_set_text(s_lbl_unit, ch->unit);

    /* Min / max */
    if (!isnan(ch->value_min))
        lv_label_set_text_fmt(s_lbl_min, "Min: %.4g", (double)ch->value_min);
    if (!isnan(ch->value_max))
        lv_label_set_text_fmt(s_lbl_max, "Max: %.4g", (double)ch->value_max);

    /* Build chart from history ring buffer */
    lv_chart_set_all_value(s_chart, s_ser, LV_CHART_POINT_NONE);

    uint16_t n = ch->history_count;
    if (n == 0) { daq_manager_unlock(); return; }

    /* Find y range */
    s_y_min = ch->history[0];
    s_y_max = ch->history[0];
    for (uint16_t i = 1; i < n; i++) {
        float v = ch->history[i];
        if (v < s_y_min) s_y_min = v;
        if (v > s_y_max) s_y_max = v;
    }
    float span = s_y_max - s_y_min;
    if (span < 1e-6f) { s_y_min -= 1.0f; s_y_max += 1.0f; span = 2.0f; }
    float margin = span * 0.1f;
    s_y_min -= margin;
    s_y_max += margin;

    /* LVGL chart works in integer "chart units"; scale to 0..1000 */
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);

    /* Walk ring buffer in chronological order */
    uint8_t  start = (ch->history_head - n + DAQ_HISTORY_DEPTH) % DAQ_HISTORY_DEPTH;
    for (uint16_t i = 0; i < n; i++) {
        uint8_t idx = (start + i) % DAQ_HISTORY_DEPTH;
        float  fv   = ch->history[idx];
        int32_t iv  = (int32_t)(((fv - s_y_min) / (s_y_max - s_y_min)) * 1000.0f);
        if (iv < 0) iv = 0;
        if (iv > 1000) iv = 1000;
        lv_chart_set_next_value(s_chart, s_ser, iv);
    }

    lv_chart_refresh(s_chart);
    daq_manager_unlock();
}
