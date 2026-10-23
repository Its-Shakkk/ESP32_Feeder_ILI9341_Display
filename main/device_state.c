#include <stdio.h>
#include <string.h>
#include "device_state.h"
#include "app_config.h"

device_state_t     g_dev;
SemaphoreHandle_t  g_state_mutex;
QueueHandle_t      g_button_queue;
EventGroupHandle_t g_events;

void device_state_init(void)
{
    memset(&g_dev, 0, sizeof(g_dev));
    g_dev.feed1.speed_percent = 50;
    g_dev.battery_percent = 78.0f;

    g_state_mutex  = xSemaphoreCreateMutex();
    g_button_queue = xQueueCreate(BUTTON_QUEUE_LEN, sizeof(button_event_t));
    g_events       = xEventGroupCreate();
    configASSERT(g_state_mutex && g_button_queue && g_events);
}

void device_state_snapshot(device_state_t *out)
{
    STATE_LOCK();
    memcpy(out, &g_dev, sizeof(*out));
    STATE_UNLOCK();
}

void device_state_roll_day(const char *date_str)
{
    char rec[40];
    STATE_LOCK();
    int n = snprintf(rec, sizeof(rec), "%s:%lu,", date_str, (unsigned long)g_dev.today_g);
    size_t len = strlen(g_dev.history);

    // Drop the oldest records if the buffer would overflow.
    while (len + n >= HISTORY_MAX) {
        char *comma = strchr(g_dev.history, ',');
        if (!comma) { g_dev.history[0] = 0; len = 0; break; }
        size_t drop = (size_t)(comma - g_dev.history) + 1;
        memmove(g_dev.history, g_dev.history + drop, len - drop + 1);
        len -= drop;
    }
    memcpy(g_dev.history + len, rec, n + 1);
    g_dev.today_g = 0;
    STATE_UNLOCK();
}
