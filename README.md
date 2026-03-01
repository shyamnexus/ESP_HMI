# ESP HMI — DAQ Frontend for Waveshare ESP32-S3-Touch-LCD-4.3B

A production-quality, touch-enabled HMI (Human-Machine Interface) built with
**ESP-IDF v5** and **LVGL v9** for connecting to multiple Data Acquisition (DAQ)
systems over UART, Wi-Fi TCP, and MQTT.

---

## Hardware

| Component | Details |
|-----------|---------|
| Board | Waveshare ESP32-S3-Touch-LCD-4.3B |
| MCU | ESP32-S3 Xtensa LX7 dual-core @ 240 MHz |
| Display | 4.3″ RGB LCD, 800×480, 65K colours |
| Touch | GT911 capacitive (5-point), I2C |
| Memory | 8 MB OPI PSRAM + 16 MB Flash |
| IO Expander | CH422G (backlight + touch reset) |
| Connectivity | 2.4 GHz Wi-Fi 802.11 b/g/n + BT 5 (LE) |

### Key GPIO Assignments

| Signal | GPIO |
|--------|------|
| LCD PCLK | 7 |
| LCD HSYNC | 46 |
| LCD VSYNC | 3 |
| LCD DE | 5 |
| LCD R\[3..7\] | 1, 2, 42, 41, 40 |
| LCD G\[2..7\] | 39, 0, 45, 48, 47, 21 |
| LCD B\[3..7\] | 14, 38, 18, 17, 10 |
| Touch SDA | 8 |
| Touch SCL | 9 |
| Touch INT | 4 |
| IO Expander SDA | 8 (shared) |
| IO Expander SCL | 9 (shared) |

> **Note:** If the display shows a blank/white screen or incorrect colours, verify
> the GPIO pin mapping against the Waveshare schematic for your board revision.
> The pixel-clock polarity (`pclk_active_neg`) and sync timing parameters in
> `bsp_lcd.c` may also need tuning.

---

## Project Structure

```
ESP_HMI/
├── CMakeLists.txt             # ESP-IDF project root
├── sdkconfig.defaults         # Pre-tuned build configuration
├── partitions.csv             # 4 MB app + 12 MB SPIFFS on 16 MB flash
├── main/
│   ├── main.c                 # App entry point
│   └── idf_component.yml      # External component dependencies
└── components/
    ├── bsp/                   # Board Support Package
    │   ├── bsp.h              # Public API + pin definitions
    │   ├── bsp_lcd.c          # RGB panel init (esp_lcd_new_rgb_panel)
    │   ├── bsp_touch.c        # GT911 via esp_lcd_touch_gt911
    │   └── bsp_io_expander.c  # CH422G backlight + touch reset
    │
    ├── daq/                   # Data Acquisition layer
    │   ├── daq_types.h        # daq_device_t, daq_channel_t types
    │   ├── daq_manager.h/.c   # Multi-device manager + NVS persistence
    │   ├── daq_uart.c         # UART polling driver (JSON protocol)
    │   └── daq_wifi.c         # Wi-Fi STA init + MQTT event driver
    │
    └── hmi/                   # LVGL v9 UI
        ├── hmi.h              # Public API, colour palette, layout consts
        ├── hmi_main.c         # LVGL port init, root layout, navigation
        └── screens/
            ├── screen_dashboard.c  # Channel card grid (real-time values)
            ├── screen_channel.c    # Full-screen chart + min/max
            ├── screen_devices.c    # Add / remove DAQ devices
            └── screen_settings.c  # Wi-Fi, brightness, about
```

---

## UI Overview

### Dashboard (default screen)
Scrollable grid of **channel cards** (one per DAQ channel). Each card shows:
- Channel name and engineering unit
- Current value (auto-formatted with `%.3g`)
- Colour-coded status dot: 🟢 Normal · 🟡 Warning · 🔴 Alarm · ⚫ Stale

Tap any card to open the **Channel Detail** screen.

### Channel Detail
- Full-width **LVGL line chart** of the last 120 samples (ring buffer, ~2 min at 1 Hz)
- Y-axis auto-scaled from historical min/max
- Running min and max values
- Back button returns to Dashboard

### Devices
- List of configured DAQ devices with live status badges
- **Add Device** form:
  - **UART**: port number (0/1/2) + baud rate
  - **MQTT**: broker URI + subscribe topic
- Remove any device with the trash icon
- Configuration saved to NVS automatically

### Settings
- Wi-Fi SSID / password with one-tap connect
- Backlight brightness slider
- "Save All" persists DAQ device configs to NVS
- Board info panel

---

## DAQ Wire Protocol

Both UART and MQTT drivers parse the same newline-terminated JSON frame:

```json
{"id":"DAQ1","ch":[{"n":"V_bus","v":24.1,"u":"V"},{"n":"I_load","v":1.23,"u":"A"},{"n":"Temp","v":42.5,"u":"°C"}]}
```

| Field | Description |
|-------|-------------|
| `id` | Device identifier (optional, used for routing) |
| `ch` | Array of channel objects |
| `n` | Channel name (up to 23 chars) |
| `v` | Numeric value (float) |
| `u` | Engineering unit string (up to 11 chars) |

### UART polling
The HMI sends `"READ\n"` on the configured UART port every `poll_interval_ms`
(default 1000 ms) and expects a JSON frame in response within 500 ms.

### MQTT
Subscribe to the configured topic. The broker pushes JSON frames; the HMI
updates channels automatically on receipt.

---

## Building

### Prerequisites

- [ESP-IDF v5.1+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
- Python 3.8+
- IDF Component Manager (included with ESP-IDF)

### Steps

```bash
# 1. Clone and enter the project
git clone https://github.com/shyamnexus/ESP_HMI.git
cd ESP_HMI

# 2. Set up ESP-IDF environment
. $IDF_PATH/export.sh       # Linux / macOS
# or: $IDF_PATH\export.bat  # Windows CMD

# 3. Set target
idf.py set-target esp32s3

# 4. Download managed components (LVGL, esp_lvgl_port, GT911, CH422G)
idf.py update-dependencies

# 5. Configure (optional – sdkconfig.defaults covers most settings)
idf.py menuconfig

# 6. Build
idf.py build

# 7. Flash (hold BOOT then press RESET to enter download mode)
idf.py -p /dev/ttyUSB0 flash monitor
```

### LVGL Configuration

LVGL options are controlled via `idf.py menuconfig → Component config → LVGL`.
The important defaults (already set in `sdkconfig.defaults`):

| Setting | Value |
|---------|-------|
| Color depth | 16-bit (RGB565) |
| Font Montserrat | 12, 14, 28 (enable in menuconfig) |

> Enable `LV_FONT_MONTSERRAT_12`, `LV_FONT_MONTSERRAT_14`, `LV_FONT_MONTSERRAT_28`
> under `Component config → LVGL → Font usage` in menuconfig, or add these to
> `sdkconfig.defaults`:
> ```
> CONFIG_LV_FONT_MONTSERRAT_12=y
> CONFIG_LV_FONT_MONTSERRAT_14=y
> CONFIG_LV_FONT_MONTSERRAT_28=y
> ```

---

## Extending the Project

### Adding a new DAQ backend

1. Implement `daq_<type>_connect`, `daq_<type>_poll`, and `daq_<type>_disconnect`
   functions (model after `daq_uart.c`).
2. Add a new `DAQ_CONN_*` variant to `daq_conn_type_t` in `daq_types.h`.
3. Dispatch to it in `daq_manager.c` (`daq_connect_task` and `daq_poll_task`).
4. Optionally add a UI form row in `screen_devices.c`.

### Adding alarm thresholds via UI

Call `daq_manager_set_thresholds(dev_idx, ch_idx, warn_lo, warn_hi, alarm_lo, alarm_hi)`
to configure per-channel thresholds at runtime. The manager evaluates them on
every value update and drives the card status colour accordingly.

### Enabling true PWM backlight dimming

Replace the `bsp_backlight_set(bool)` implementation with an LEDC PWM output
on a suitable GPIO. The Settings slider already maps 0–100% to this function.

---

## License

MIT — see `LICENSE` file (add one as needed).
