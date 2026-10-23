#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "device_state.h"

// While UP/DOWN stays held after the long-press threshold (BUTTON_LONG_MS),
// the long event is re-sent every BUTTON_REPEAT_MS until the button is released.
#ifndef BUTTON_REPEAT_MS
#define BUTTON_REPEAT_MS    500
#endif

typedef struct {
    gpio_num_t     pin;
    button_event_t ev_short;
    bool           has_long;
    button_event_t ev_long;
    bool           repeat;          // auto-repeat the long event while held
    bool           cur;             // debounced level (true = released)
    bool           last;            // last raw reading
    uint32_t       debounce_ms;
    uint32_t       press_start_ms;
    uint32_t       next_repeat_ms;
    bool           long_handled;
} button_t;

static button_t s_btn[] = {
    { .pin = PIN_BTN_UP,    .ev_short = BTN_EVT_UP,       .has_long = true,  .ev_long = BTN_EVT_UP_LONG,   .repeat = true,  .cur = true, .last = true },
    { .pin = PIN_BTN_DOWN,  .ev_short = BTN_EVT_DOWN,     .has_long = true,  .ev_long = BTN_EVT_DOWN_LONG, .repeat = true,  .cur = true, .last = true },
    { .pin = PIN_BTN_LEFT,  .ev_short = BTN_EVT_LEFT,     .has_long = false,                               .repeat = false, .cur = true, .last = true },
    { .pin = PIN_BTN_RIGHT, .ev_short = BTN_EVT_RIGHT,    .has_long = false,                               .repeat = false, .cur = true, .last = true },
    { .pin = PIN_BTN_OK,    .ev_short = BTN_EVT_OK_SHORT, .has_long = true,  .ev_long = BTN_EVT_OK_LONG,   .repeat = false, .cur = true, .last = true },
};
#define NUM_BTN (sizeof(s_btn) / sizeof(s_btn[0]))

static inline uint32_t millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void post(button_event_t ev)
{
    xQueueSend(g_button_queue, &ev, 0);
}

void button_task(void *arg)
{
    for (size_t i = 0; i < NUM_BTN; i++) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << s_btn[i].pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
    }

    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        uint32_t now = millis();
        for (size_t i = 0; i < NUM_BTN; i++) {
            button_t *b = &s_btn[i];
            bool reading = gpio_get_level(b->pin);

            if (reading != b->last) b->debounce_ms = now;

            if ((now - b->debounce_ms) > BUTTON_DEBOUNCE_MS && reading != b->cur) {
                b->cur = reading;
                if (b->cur == 0) {                  // pressed
                    b->press_start_ms = now;
                    b->long_handled = false;
                } else if (!b->long_handled) {      // released after a short press
                    post(b->ev_short);
                }
            }

            if (b->cur == 0 && !b->long_handled && (now - b->press_start_ms) > BUTTON_LONG_MS) {
                b->long_handled = true;             // LEFT/RIGHT swallow long presses (as original)
                if (b->has_long) post(b->ev_long);
                b->next_repeat_ms = now + BUTTON_REPEAT_MS;
            } else if (b->cur == 0 && b->long_handled && b->repeat &&
                       (int32_t)(now - b->next_repeat_ms) >= 0) {
                post(b->ev_long);                   // still held -> repeat
                b->next_repeat_ms += BUTTON_REPEAT_MS;
            }
            b->last = reading;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}