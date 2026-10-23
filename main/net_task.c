#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "device_state.h"

static const char *TAG = "net";

#define RECONNECT_DELAY_MS 5000

static volatile bool     s_reconnect_pending;
static volatile int64_t  s_reconnect_at_ms;
static bool              s_sntp_started;

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            xEventGroupClearBits(g_events, EVT_WIFI_CONNECTED_BIT);
            s_reconnect_at_ms = now_ms() + RECONNECT_DELAY_MS;
            s_reconnect_pending = true;
            xEventGroupSetBits(g_events, EVT_NET_CHANGED_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(g_events, EVT_WIFI_CONNECTED_BIT | EVT_NET_CHANGED_BIT);
    }
}

static void time_sync_cb(struct timeval *tv)
{
    xEventGroupSetBits(g_events, EVT_TIME_SYNCED_BIT);
}

static void wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, CONFIG_FEEDER_WIFI_SSID, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_FEEDER_WIFI_PASSWORD, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;      // accept WPA/WPA2/WPA3 networks

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void sntp_start_once(void)
{
    if (s_sntp_started) return;
    s_sntp_started = true;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_FEEDER_NTP_SERVER);
    sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started (%s)", CONFIG_FEEDER_NTP_SERVER);
}

// Detects midnight rollover (IST) and archives today's grams into history.
static void check_date_rollover(void)
{
    static char last_date[12] = "";

    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);
    if (t.tm_year < (2020 - 1900)) return;          // clock not set yet

    char cur[12];
    strftime(cur, sizeof(cur), "%d-%b-%Y", &t);
    if (strcmp(cur, last_date) != 0) {
        if (last_date[0] != 0) {
            device_state_roll_day(last_date);
            storage_save();
        }
        strcpy(last_date, cur);
    }
}

void net_task(void *arg)
{
    setenv("TZ", CONFIG_FEEDER_TIMEZONE, 1);
    tzset();
    wifi_start();

    for (;;) {
        // Wakes immediately on a WiFi event, otherwise once per second.
        xEventGroupWaitBits(g_events, EVT_NET_CHANGED_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));

        bool connected = (xEventGroupGetBits(g_events) & EVT_WIFI_CONNECTED_BIT) != 0;

        STATE_LOCK();
        g_dev.wifi_connected = connected;
        STATE_UNLOCK();

        if (connected) {
            sntp_start_once();
        } else if (s_reconnect_pending && now_ms() >= s_reconnect_at_ms) {
            s_reconnect_pending = false;
            esp_wifi_connect();
        }

        check_date_rollover();
    }
}
