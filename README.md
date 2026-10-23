# Feeder Controller - ESP-IDF (ESP32 WROOM-1 DevKit)

Port of `feeder_esp32devkit.ino` to ESP-IDF (C, FreeRTOS tasks).
Display: ILI9341, **landscape 320x240**, same UI layout as the Arduino version.
Feed consumption is now in **grams** (was mg).

## Build / flash

Tested layout for ESP-IDF **v5.1 or newer** (target `esp32`).

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32
idf.py menuconfig        # optional: "Feeder Configuration" -> WiFi SSID / password / NTP / timezone
idf.py build flash monitor
```

WiFi defaults in `main/Kconfig.projbuild` are the ones from your sketch.
Change them in menuconfig (they are stored in `sdkconfig`, not in the C code).

## Pins (unchanged)

| Function | GPIO |
|---|---|
| TFT SCK / MOSI | 18 / 23 (SPI3 = VSPI) |
| TFT CS / DC / RST / BLK | 22 / 2 / 21 / 5 |
| Buttons UP / DOWN / LEFT / RIGHT / OK | 14 / 26 / 13 / 32 / 27 (active low, internal pull-up) |
| Motor 1 (relay) | 4 |
| Motor 2 (PWM, 5 kHz, 8 bit) | 15 |

All pins and the resolution (`LCD_H_RES 320`, `LCD_V_RES 240`) are in `main/app_config.h`.
If the picture is mirrored/rotated on your panel change `LCD_MADCTL` (0x28 default; try 0xE8, 0x88, 0x48).

## Task architecture

```
app_main() -> device_state_init() (mutex, button queue, event group)
              storage_init()      (NVS load)
              motor_hw_init(), lcd_init()
              creates 4 tasks

button_task  prio 4  20 ms polling, debounce 50 ms, long-press 1 s -> button_queue
motor_task   prio 5  1 s tick: feed countdown / run time / grams booking / NVS save
net_task     prio 3  event-driven (WiFi events, 1 s timeout): WiFi, SNTP, midnight history roll
display_task prio 3  waits on button_queue (100 ms timeout): UI state machine + ILI9341 drawing
```

Shared data lives in `g_dev` (`device_state.h`), always accessed under `g_state_mutex`.
Only `display_task` touches the LCD. Motor 2 delayed shutdown (2 s after relay off) uses a
one-shot `esp_timer`, so it is accurate and independent of the 1 s motor tick.

## Files

```
main/main.c            app_main, task creation
main/device_state.*    shared state, queue / mutex / event group
main/storage.c         NVS load/save/reset (namespace "feeder")
main/lcd_ili9341.*     SPI driver + 5x7 text/rect primitives (no external components)
main/font5x7.h         classic 6x8 cell font (same look as Adafruit GFX)
main/motor.*           relay + LEDC PWM + delayed shutdown
main/button_task.c     buttons
main/motor_task.c      feed scheduler
main/net_task.c        WiFi + SNTP + date rollover
main/display_task.c    all screens + state machine
partitions.csv         OTA layout (ota_0 / ota_1 / otadata, 4 MB flash)
```

## Changes vs. the Arduino sketch

* Consumption is stored/shown in grams: NVS keys `consG`, `todayG`, `lifeG`; Add Feed step is
  1 g (long press 10 g, max 100000 g). Old mg values in NVS are ignored (new key names).
* History is capped at 512 bytes; oldest records are dropped when full.
* The day rollover only needs the SNTP clock to be set (it keeps working if WiFi drops).
* No external LCD component: the ILI9341 driver is included, so nothing is downloaded at build time.

## OTA and flash encryption (later)

* `partitions.csv` already has `otadata`, `ota_0`, `ota_1` (1.7 MB each) and an `nvs_key` partition.
* Flash encryption: enable in menuconfig (Security features). Use *Development* mode first;
  *Release* mode is irreversible. Bootloader gets larger with encryption enabled - the layout
  leaves room (bootloader at 0x1000, table at 0x8000).
* To also encrypt NVS, enable NVS encryption (uses the `nvs_key` partition) and read
  `nvs_flash_secure_init()` docs; `storage_init()` is the only place that calls `nvs_flash_init()`.
* Keep WiFi credentials out of git if you share the project (`sdkconfig` contains them).
