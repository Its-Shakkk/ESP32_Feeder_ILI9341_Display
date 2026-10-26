#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "device_state.h"
#include "motor_hw.h"

static const char *TAG = "motor_task";

// 1 Hz feed scheduler: counts down to the next feed, runs the motors for the
// configured run time, then books the consumption.
// Rules when the hopper (remaining_g) is empty:
//   * a scheduled feed is NOT started (the countdown just restarts),
//   * a run in progress is ended,
//   * booked consumption never exceeds what is left in the hopper.
void motor_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    ESP_LOGI(TAG, "started (hopper-aware: no feeding and no booking when empty)");

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(MOTOR_TICK_MS));

        bool do_start = false, do_stop = false, do_save = false, skipped_empty = false;
        uint8_t speed = 0;
        uint32_t used = 0, left = 0;

        STATE_LOCK();
        feed_config_t *f = &g_dev.feed1;

        if (f->run_time_sec == 0 && f->interval_min == 0 && f->interval_sec == 0) {
            g_dev.motor_on = false;
            STATE_UNLOCK();
            continue;
        }
        if (g_dev.configuring_interval) {
            STATE_UNLOCK();
            continue;
        }

        if (g_dev.motor_on) {
            if (g_dev.run_time_remaining_sec > 0 && g_dev.remaining_g > 0) {
                g_dev.run_time_remaining_sec--;
            } else {
                // run finished (or hopper ran empty): stop and book what was really used
                g_dev.motor_on = false;
                do_stop = true;

                used = f->consumption_per_run_g;
                if (used > g_dev.remaining_g) used = g_dev.remaining_g;
                g_dev.remaining_g -= used;
                g_dev.today_g     += used;
                g_dev.lifetime_g  += used;
                left = g_dev.remaining_g;
                do_save = true;

                g_dev.next_feed_remaining_sec = (uint32_t)f->interval_min * 60;
                if (g_dev.next_feed_remaining_sec == 0) g_dev.next_feed_remaining_sec = 60;
                g_dev.run_time_remaining_sec = f->run_time_sec;
            }
        } else {
            if (g_dev.next_feed_remaining_sec > 0) {
                g_dev.next_feed_remaining_sec--;
            } else if (f->enabled && f->run_time_sec > 0 && g_dev.remaining_g > 0) {
                g_dev.motor_on = true;
                g_dev.run_time_remaining_sec = f->run_time_sec;
                do_start = true;
                speed = f->speed_percent;
            } else {
                // Feed is due but the hopper is empty (or feeding is not enabled): skip it.
                skipped_empty = f->enabled && f->run_time_sec > 0;
                uint32_t period = (uint32_t)f->interval_min * 60;
                g_dev.next_feed_remaining_sec = (skipped_empty && period) ? period : 60;
            }
        }
        STATE_UNLOCK();

        if (do_stop)  motor_stop();
        if (do_start) motor_start(speed);
        if (do_save) {
            storage_save();
            ESP_LOGI(TAG, "feed booked: %lu g, remaining %lu g", (unsigned long)used, (unsigned long)left);
        }
        if (skipped_empty) ESP_LOGW(TAG, "feed skipped: hopper empty - add feed");
    }
}