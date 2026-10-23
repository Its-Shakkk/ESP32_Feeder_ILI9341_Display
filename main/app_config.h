#pragma once

// =========================================================================
// Hardware / resolution configuration  -  ESP32 WROOM-1 DevKit
// =========================================================================

// --- Display Resolution (landscape) ---
#define LCD_H_RES       320
#define LCD_V_RES       240

// ILI9341 MADCTL for landscape: MV | BGR (0x28). If the picture is mirrored or
// upside-down on your panel use 0xE8 (180 deg), 0x88 or 0x48.
#define LCD_MADCTL      0x48

// LCD_MADCTL	Orientation (Adafruit rotation)	Fits the 320×240 UI?
// 0x28	Landscape (rotation 1), my default	Yes
// 0xE8	Landscape rotated 180° (rotation 3)	Yes
// 0x48	Portrait (rotation 0)	No
// 0x88	Portrait rotated 180° (rotation 2)	No

// --- TFT pins (SPI3 / VSPI) ---
#define PIN_TFT_SCK     18
#define PIN_TFT_MISO    -1      // write-only
#define PIN_TFT_MOSI    23
#define PIN_TFT_CS      22
#define PIN_TFT_DC      2
#define PIN_TFT_RST     21
#define PIN_TFT_BLK     5
#define LCD_SPI_HOST    SPI3_HOST
#define LCD_SPI_HZ      (40 * 1000 * 1000)

// --- Buttons (active low, internal pull-up) ---
#define PIN_BTN_UP      14
#define PIN_BTN_DOWN    26
#define PIN_BTN_LEFT    13
#define PIN_BTN_RIGHT   32
#define PIN_BTN_OK      27

// --- Motors ---
#define PIN_MOTOR1      4       // Relay ON/OFF
#define PIN_MOTOR2      15      // PWM controlled

#define MOTOR2_PWM_FREQ_HZ        5000
#define MOTOR2_SHUTDOWN_DELAY_MS  2000

// --- Task parameters ---
#define BUTTON_TASK_PRIO    4
#define MOTOR_TASK_PRIO     5
#define NET_TASK_PRIO       3
#define DISPLAY_TASK_PRIO   3

#define BUTTON_TASK_STACK   2048
#define MOTOR_TASK_STACK    4096
#define NET_TASK_STACK      6144
#define DISPLAY_TASK_STACK  6144

#define BUTTON_POLL_MS      20
#define MOTOR_TICK_MS       1000
#define DISPLAY_TICK_MS     100
#define BUTTON_DEBOUNCE_MS  50
#define BUTTON_LONG_MS      1000
#define BUTTON_QUEUE_LEN    16

// --- Limits (feed consumption is in GRAMS) ---
#define FEED_QTY_MAX_KG         100
#define FEED_RUN_MAX_SEC        60
#define FEED_CONSUM_MAX_G       100000
