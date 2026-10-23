#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "device_state.h"
#include "motor.h"

// 1 Hz feed scheduler: counts down to the next feed, runs the motors for the
// configured run time, then books the consumption (grams).
void motor_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(MOTOR_TICK_MS));

        bool do_start = false, do_stop = false, do_save = false;
        uint8_t speed = 0;

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
            if (g_dev.run_time_remaining_sec > 0) {
                g_dev.run_time_remaining_sec--;
            } else {
                g_dev.motor_on = false;
                do_stop = true;

                g_dev.today_g    += f->consumption_per_run_g;
                g_dev.lifetime_g += f->consumption_per_run_g;
                do_save = true;

                g_dev.next_feed_remaining_sec = (uint32_t)f->interval_min * 60;
                if (g_dev.next_feed_remaining_sec == 0) g_dev.next_feed_remaining_sec = 60;
                g_dev.run_time_remaining_sec = f->run_time_sec;
            }
        } else {
            if (g_dev.next_feed_remaining_sec > 0) {
                g_dev.next_feed_remaining_sec--;
            } else if (f->enabled && f->run_time_sec > 0) {
                g_dev.motor_on = true;
                g_dev.run_time_remaining_sec = f->run_time_sec;
                do_start = true;
                speed = f->speed_percent;
            } else {
                g_dev.next_feed_remaining_sec = 60;
            }
        }
        STATE_UNLOCK();

        if (do_stop)  motor_stop();
        if (do_start) motor_start(speed);
        if (do_save)  storage_save();
    }
}
