#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#define HISTORY_MAX 512

// ---- Event group bits ----
#define EVT_WIFI_CONNECTED_BIT  (1 << 0)
#define EVT_TIME_SYNCED_BIT     (1 << 1)
#define EVT_NET_CHANGED_BIT     (1 << 2)

// ---- Button events (button_task -> display_task) ----
typedef enum {
    BTN_EVT_UP,
    BTN_EVT_DOWN,
    BTN_EVT_LEFT,
    BTN_EVT_RIGHT,
    BTN_EVT_OK_SHORT,
    BTN_EVT_OK_LONG,
    BTN_EVT_UP_LONG,
    BTN_EVT_DOWN_LONG,
} button_event_t;

typedef struct {
    uint16_t interval_min;
    uint16_t interval_sec;
    uint16_t run_time_sec;
    uint32_t quantity_g;                // hopper size set in Add Feed (grams, shown as kg)
    uint32_t consumption_per_run_g;     // grams per feed run
    uint8_t  speed_percent;
    bool     enabled;
} feed_config_t;

typedef struct {
    feed_config_t feed1;
    uint32_t today_g;
    uint32_t lifetime_g;
    uint32_t remaining_g;               // feed left in the hopper (refilled by Add Feed)

    // runtime (not persisted)
    bool     motor_on;
    bool     configuring_interval;
    bool     wifi_connected;
    float    battery_percent;
    uint32_t next_feed_remaining_sec;
    uint32_t run_time_remaining_sec;

    // "dd-Mon-yyyy:grams," records
    char     history[HISTORY_MAX];
} device_state_t;

extern device_state_t     g_dev;
extern SemaphoreHandle_t  g_state_mutex;
extern QueueHandle_t      g_button_queue;
extern EventGroupHandle_t g_events;

#define STATE_LOCK()    xSemaphoreTake(g_state_mutex, portMAX_DELAY)
#define STATE_UNLOCK()  xSemaphoreGive(g_state_mutex)

// Creates mutex / queue / event group and loads defaults. Call first.
void device_state_init(void);

// Thread-safe copy of the whole state.
void device_state_snapshot(device_state_t *out);

// Appends a daily record to history and zeroes today's counter (thread-safe).
void device_state_roll_day(const char *date_str);

// ---- storage.c (NVS) ----
void storage_init(void);        // nvs_flash_init + load into g_dev
void storage_save(void);        // snapshot g_dev -> NVS
void storage_reset(void);       // erase namespace and reset g_dev config

// ---- tasks ----
void button_task(void *arg);
void motor_task(void *arg);
void net_task(void *arg);
void display_task(void *arg);