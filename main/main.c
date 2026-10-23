#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "device_state.h"
#include "lcd_ili9341.h"
#include "motor.h"

static const char *TAG = "feeder";

void app_main(void)
{
    ESP_LOGI(TAG, "Feeder controller starting");

    // 1. Shared RTOS objects (state mutex, button queue, event group)
    device_state_init();

    // 2. Persistent config (NVS) -> g_dev
    storage_init();

    // 3. Hardware
    motor_hw_init();
    lcd_init();

    // 4. Tasks
    xTaskCreate(button_task,  "button_task",  BUTTON_TASK_STACK,  NULL, BUTTON_TASK_PRIO,  NULL);
    xTaskCreate(motor_task,   "motor_task",   MOTOR_TASK_STACK,   NULL, MOTOR_TASK_PRIO,   NULL);
    xTaskCreate(net_task,     "net_task",     NET_TASK_STACK,     NULL, NET_TASK_PRIO,     NULL);
    xTaskCreate(display_task, "display_task", DISPLAY_TASK_STACK, NULL, DISPLAY_TASK_PRIO, NULL);
}
