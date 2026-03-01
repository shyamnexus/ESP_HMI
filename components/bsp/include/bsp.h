#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Waveshare ESP32-S3-Touch-LCD-4.3B – hardware constants
 *
 * Pinout verified against the Waveshare wiki schematic.
 * If you have a different board revision, cross-check every
 * GPIO against the actual silkscreen / schematic.
 * ============================================================ */

/* --- Display geometry ---------------------------------------- */
#define BSP_LCD_H_RES           800
#define BSP_LCD_V_RES           480

/* Pixel clock for ~40 FPS at 800×480.
 * Adjust if you see tearing or blank frames. */
#define BSP_LCD_PCLK_HZ         (14 * 1000 * 1000)

/* --- RGB panel control signals ------------------------------- */
#define BSP_LCD_PCLK_GPIO       GPIO_NUM_7
#define BSP_LCD_HSYNC_GPIO      GPIO_NUM_46
#define BSP_LCD_VSYNC_GPIO      GPIO_NUM_3
#define BSP_LCD_DE_GPIO         GPIO_NUM_5
#define BSP_LCD_DISP_GPIO       GPIO_NUM_NC   /* Backlight via CH422G */

/* --- RGB565 data lines (bit 0 = B[0], bit 15 = R[4]) --------- */
/* Blue  [B3..B7]  → data bits [0..4]  */
#define BSP_LCD_B0_GPIO         GPIO_NUM_14   /* B3 */
#define BSP_LCD_B1_GPIO         GPIO_NUM_38   /* B4 */
#define BSP_LCD_B2_GPIO         GPIO_NUM_18   /* B5 */
#define BSP_LCD_B3_GPIO         GPIO_NUM_17   /* B6 */
#define BSP_LCD_B4_GPIO         GPIO_NUM_10   /* B7 */
/* Green [G2..G7]  → data bits [5..10] */
#define BSP_LCD_G0_GPIO         GPIO_NUM_39   /* G2 */
#define BSP_LCD_G1_GPIO         GPIO_NUM_0    /* G3 */
#define BSP_LCD_G2_GPIO         GPIO_NUM_45   /* G4 */
#define BSP_LCD_G3_GPIO         GPIO_NUM_48   /* G5 */
#define BSP_LCD_G4_GPIO         GPIO_NUM_47   /* G6 */
#define BSP_LCD_G5_GPIO         GPIO_NUM_21   /* G7 */
/* Red   [R3..R7]  → data bits [11..15] */
#define BSP_LCD_R0_GPIO         GPIO_NUM_1    /* R3 */
#define BSP_LCD_R1_GPIO         GPIO_NUM_2    /* R4 */
#define BSP_LCD_R2_GPIO         GPIO_NUM_42   /* R5 */
#define BSP_LCD_R3_GPIO         GPIO_NUM_41   /* R6 */
#define BSP_LCD_R4_GPIO         GPIO_NUM_40   /* R7 */

/* --- RGB panel sync timings (800×480 panel typical values) --- */
#define BSP_LCD_HSYNC_BP        40
#define BSP_LCD_HSYNC_FP        48
#define BSP_LCD_HSYNC_PW        48
#define BSP_LCD_VSYNC_BP        13
#define BSP_LCD_VSYNC_FP        3
#define BSP_LCD_VSYNC_PW        3

/* --- Capacitive touch (GT911 via I2C) ------------------------ */
#define BSP_TOUCH_I2C_PORT      I2C_NUM_0
#define BSP_TOUCH_SDA_GPIO      GPIO_NUM_8
#define BSP_TOUCH_SCL_GPIO      GPIO_NUM_9
#define BSP_TOUCH_INT_GPIO      GPIO_NUM_4
#define BSP_TOUCH_I2C_FREQ_HZ   400000

/* --- CH422G IO expander – EXIO pin bit positions ------------- */
/* The CH422G uses I2C address 0x24 (mode) and 0x38 (output);
 * these constants identify which bit in the output byte controls
 * each function.  See bsp_io_expander.c for the full protocol. */
#define BSP_IO_TOUCH_RST_PIN    0   /* bit 0 = EXIO1 – GT911 reset (active-low) */
#define BSP_IO_LCD_BL_PIN       1   /* bit 1 = EXIO2 – LCD backlight enable      */

/* ============================================================
 * Public types
 * ============================================================ */

typedef struct {
    i2c_master_bus_handle_t  i2c_bus;    /**< Shared I2C bus (touch + CH422G) */
    esp_lcd_panel_handle_t   lcd_panel;  /**< RGB LCD panel handle             */
    esp_lcd_touch_handle_t   touch;      /**< GT911 touch handle               */
} bsp_handles_t;

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * @brief  Initialise all board peripherals in the correct order.
 *
 * Sequence: I2C bus → CH422G (touch in reset, BL off) →
 *           touch reset pulse → RGB LCD panel → GT911 touch → BL on.
 */
esp_err_t bsp_init(void);

/** Return the singleton handles populated by bsp_init(). */
const bsp_handles_t *bsp_get_handles(void);

/** Turn the LCD backlight on (true) or off (false). */
esp_err_t bsp_backlight_set(bool on);

#ifdef __cplusplus
}
#endif
