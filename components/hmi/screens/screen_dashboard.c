/**
 * @file screen_dashboard.c
 * @brief Main dashboard: scrollable grid of channel cards per DAQ device.
 *
 * Layout (800 × 380 content area):
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │  [ DAQ Device Name — status badge ]                                 │
 *   │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐              │
 *   │  │  Ch1     │ │  Ch2     │ │  Ch3     │ │  Ch4     │              │
 *   │  │ 3.14  V  │ │ 0.50  A  │ │ 25.1 °C  │ │ 98.3  %  │              │
 *   │  └──────────┘ └──────────┘ └──────────┘ └──────────┘              │
 *   │  (next device …)                                                    │
 *   └─────────────────────────────────────────────────────────────────────┘
 *
 * Tap any channel card to open the detailed channel view.
 */

#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "hmi.h"
#include "daq_manager.h"

#define CARD_W          182
#define CARD_H          110
#define CARD_GAP        8
#define SECTION_PAD_H   10
#define SECTION_PAD_V   8

/* Per-card user data: encodes (dev_idx << 8 | ch_idx) */
#define CARD_UD(d, c)   ((void *)(intptr_t)(((d) << 8) | (c)))
#define CARD_DEV(ud)    ((uint8_t)(((intptr_t)(ud)) >> 8))
#define CARD_CH(ud)     ((uint8_t)(((intptr_t)(ud)) & 0xFF))

/* Dynamic label references so we can refresh without rebuilding */
typedef struct {
    lv_obj_t *card;
    lv_obj_t *lbl_name;
    lv_obj_t *lbl_value;
    lv_obj_t *lbl_unit;
    lv_obj_t *status_dot;
} ch_card_t;

#define MAX_CARDS  (DAQ_MAX_DEVICES * DAQ_MAX_CHANNELS)
static ch_card_t  s_cards[MAX_CARDS];
static uint16_t   s_card_count = 0;

/* Device header labels for status refresh */
static lv_obj_t  *s_dev_status_lbl[DAQ_MAX_DEVICES];
static uint8_t    s_dev_count_built = 0;

static lv_obj_t  *s_panel = NULL;
static lv_obj_t  *s_scroll = NULL;

/* ============================================================
 * Channel-card click handler → navigate to detail screen
 * ============================================================ */
static void card_click_cb(lv_event_t *e)
{
    void *ud = lv_event_get_user_data(e);
    uint8_t dev_idx = CARD_DEV(ud);
    uint8_t ch_idx  = CARD_CH(ud);
    screen_channel_show(dev_idx, ch_idx);
    hmi_navigate(HMI_PANEL_CHANNEL);
}

/* ============================================================
 * Build a single channel card
 * ============================================================ */
static ch_card_t build_card(lv_obj_t *parent, uint8_t dev_idx, uint8_t ch_idx,
                             const daq_channel_t *ch)
{
    ch_card_t c = {0};

    c.card = lv_obj_create(parent);
    lv_obj_set_size(c.card, CARD_W, CARD_H);
    hmi_style_card(c.card);
    lv_obj_set_style_pad_all(c.card, 10, 0);
    lv_obj_clear_flag(c.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c.card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c.card, card_click_cb, LV_EVENT_CLICKED,
                        CARD_UD(dev_idx, ch_idx));

    /* Coloured status indicator dot (top-right corner) */
    c.status_dot = lv_obj_create(c.card);
    lv_obj_set_size(c.status_dot, 8, 8);
    lv_obj_set_style_radius(c.status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c.status_dot, hmi_status_color(ch->status), 0);
    lv_obj_set_style_bg_opa(c.status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c.status_dot, 0, 0);
    lv_obj_align(c.status_dot, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* Channel name */
    c.lbl_name = lv_label_create(c.card);
    lv_label_set_text(c.lbl_name, ch->name[0] ? ch->name : "---");
    hmi_style_label_muted(c.lbl_name);
    lv_obj_align(c.lbl_name, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Large value */
    c.lbl_value = lv_label_create(c.card);
    lv_label_set_text_fmt(c.lbl_value, "%.2f", (double)ch->value);
    hmi_style_label_value(c.lbl_value);
    lv_obj_align(c.lbl_value, LV_ALIGN_LEFT_MID, 0, 8);

    /* Unit */
    c.lbl_unit = lv_label_create(c.card);
    lv_label_set_text(c.lbl_unit, ch->unit[0] ? ch->unit : "");
    hmi_style_label_unit(c.lbl_unit);
    lv_obj_align(c.lbl_unit, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    return c;
}

/* ============================================================
 * Build device section (header row + card grid)
 * ============================================================ */
static void build_device_section(lv_obj_t *parent, uint8_t dev_idx,
                                  const daq_device_t *dev)
{
    /* Section container (vertical flex) */
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_set_width(section, HMI_DISPLAY_W - SECTION_PAD_H * 2);
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(section, 0, 0);
    lv_obj_set_style_pad_all(section, 0, 0);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(section, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* --- Device header row --- */
    lv_obj_t *header = lv_obj_create(section);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 30);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_name = lv_label_create(header);
    lv_label_set_text(lbl_name, dev->name[0] ? dev->name : dev->id);
    lv_obj_set_style_text_color(lbl_name, HMI_COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *lbl_status = lv_label_create(header);
    lv_label_set_text(lbl_status, hmi_daq_status_str(dev->status));
    lv_obj_set_style_text_color(lbl_status, hmi_daq_status_color(dev->status), 0);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_status, LV_ALIGN_RIGHT_MID, 0, 0);
    s_dev_status_lbl[dev_idx] = lbl_status;

    /* --- Card grid (wrap flex row) --- */
    lv_obj_t *grid = lv_obj_create(section);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, CARD_GAP, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    for (uint8_t c = 0; c < dev->num_channels && c < DAQ_MAX_CHANNELS; c++) {
        if (s_card_count >= MAX_CARDS) break;
        s_cards[s_card_count] = build_card(grid, dev_idx, c, &dev->channels[c]);
        s_card_count++;
    }
}

/* ============================================================
 * Public: create the dashboard panel
 * ============================================================ */
lv_obj_t *screen_dashboard_create(lv_obj_t *parent)
{
    s_panel = lv_obj_create(parent);
    lv_obj_set_style_bg_color(s_panel, HMI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);

    /* Outer scrollable container */
    s_scroll = lv_obj_create(s_panel);
    lv_obj_set_size(s_scroll, HMI_DISPLAY_W, HMI_CONTENT_H);
    lv_obj_set_pos(s_scroll, 0, 0);
    lv_obj_set_style_bg_opa(s_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_scroll, 0, 0);
    lv_obj_set_style_pad_hor(s_scroll, SECTION_PAD_H, 0);
    lv_obj_set_style_pad_ver(s_scroll, SECTION_PAD_V, 0);
    lv_obj_set_style_pad_gap(s_scroll, 12, 0);
    lv_obj_set_scroll_dir(s_scroll, LV_DIR_VER);
    lv_obj_set_layout(s_scroll, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_scroll, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Build device sections for all currently registered devices */
    s_card_count = 0;
    s_dev_count_built = daq_manager_get_device_count();

    daq_manager_lock();
    for (uint8_t i = 0; i < s_dev_count_built; i++) {
        const daq_device_t *dev = daq_manager_get_device(i);
        if (dev) build_device_section(s_scroll, i, dev);
    }
    daq_manager_unlock();

    /* Empty state label (hidden when there are devices) */
    if (s_dev_count_built == 0) {
        lv_obj_t *lbl = lv_label_create(s_scroll);
        lv_label_set_text(lbl, LV_SYMBOL_LIST "\nNo DAQ devices configured.\n"
                               "Go to Devices → Add Device.");
        lv_obj_set_style_text_color(lbl, HMI_COL_TEXT_MUTED, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
    }

    return s_panel;
}

/* ============================================================
 * Public: refresh channel values (called every 100 ms)
 * ============================================================ */
void screen_dashboard_refresh(void)
{
    if (!s_panel || s_card_count == 0) return;

    daq_manager_lock();

    uint16_t card_idx = 0;
    uint8_t dev_cnt = daq_manager_get_device_count();

    for (uint8_t d = 0; d < dev_cnt && card_idx < s_card_count; d++) {
        const daq_device_t *dev = daq_manager_get_device(d);
        if (!dev) continue;

        /* Refresh device status label */
        if (d < s_dev_count_built && s_dev_status_lbl[d]) {
            lv_label_set_text(s_dev_status_lbl[d], hmi_daq_status_str(dev->status));
            lv_obj_set_style_text_color(s_dev_status_lbl[d],
                                        hmi_daq_status_color(dev->status), 0);
        }

        for (uint8_t c = 0; c < dev->num_channels && card_idx < s_card_count; c++, card_idx++) {
            ch_card_t *card = &s_cards[card_idx];
            const daq_channel_t *ch = &dev->channels[c];

            lv_label_set_text_fmt(card->lbl_value, "%.3g", (double)ch->value);
            lv_obj_set_style_bg_color(card->status_dot,
                                      hmi_status_color(ch->status), 0);

            /* Highlight card border on alarm/warning */
            lv_color_t border = (ch->status == CH_STATUS_ALARM)   ? HMI_COL_ALARM   :
                                 (ch->status == CH_STATUS_WARNING) ? HMI_COL_WARNING  :
                                                                     HMI_COL_BORDER;
            lv_obj_set_style_border_color(card->card, border, 0);
        }
    }

    daq_manager_unlock();
}
