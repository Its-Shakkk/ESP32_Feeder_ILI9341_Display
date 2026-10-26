#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "device_state.h"

static const char *TAG = "storage";
static const char *NS  = "feeder";

static SemaphoreHandle_t s_nvs_mutex;
static device_state_t    s_snap;        // snapshot used for writing (keeps task stacks small)

static void load_defaults_locked(void)
{
    memset(&g_dev.feed1, 0, sizeof(g_dev.feed1));
    g_dev.feed1.speed_percent = 50;
    g_dev.today_g = 0;
    g_dev.lifetime_g = 0;
    g_dev.remaining_g = 0;
    g_dev.history[0] = 0;
}

void storage_init(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);
    s_nvs_mutex = xSemaphoreCreateMutex();

    nvs_handle_t h;
    STATE_LOCK();
    load_defaults_locked();
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        uint32_t v;
        uint8_t  b;
        if (nvs_get_u32(h, "intMin", &v) == ESP_OK) g_dev.feed1.interval_min = v % 100;
        if (nvs_get_u32(h, "intSec", &v) == ESP_OK) g_dev.feed1.interval_sec = v % 60;
        if (nvs_get_u32(h, "runSec", &v) == ESP_OK) g_dev.feed1.run_time_sec = v;
        if (nvs_get_u32(h, "qtyG",   &v) == ESP_OK) g_dev.feed1.quantity_g = v;
        else if (nvs_get_u32(h, "qtyKg", &v) == ESP_OK) g_dev.feed1.quantity_g = v * 1000UL;   // older firmware
        if (nvs_get_u32(h, "consG",  &v) == ESP_OK) g_dev.feed1.consumption_per_run_g = v;
        if (nvs_get_u32(h, "speed",  &v) == ESP_OK && v <= 100) g_dev.feed1.speed_percent = v;
        if (nvs_get_u8 (h, "enabled",&b) == ESP_OK) g_dev.feed1.enabled = b;
        if (nvs_get_u32(h, "todayG", &v) == ESP_OK) g_dev.today_g = v;
        if (nvs_get_u32(h, "lifeG",  &v) == ESP_OK) g_dev.lifetime_g = v;
        if (nvs_get_u32(h, "remG",   &v) == ESP_OK) {
            g_dev.remaining_g = v;
        } else {
            // Upgrading from firmware without a stored remaining value: use the old formula once.
            uint32_t total = g_dev.feed1.quantity_g;
            g_dev.remaining_g = (total > g_dev.lifetime_g) ? total - g_dev.lifetime_g : 0;
        }
        size_t len = sizeof(g_dev.history);
        if (nvs_get_str(h, "history", g_dev.history, &len) != ESP_OK) g_dev.history[0] = 0;
        nvs_close(h);
    } else {
        ESP_LOGI(TAG, "No saved config, using defaults");
    }
    STATE_UNLOCK();
}

void storage_save(void)
{
    xSemaphoreTake(s_nvs_mutex, portMAX_DELAY);
    device_state_snapshot(&s_snap);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "intMin", s_snap.feed1.interval_min);
        nvs_set_u32(h, "intSec", s_snap.feed1.interval_sec);
        nvs_set_u32(h, "runSec", s_snap.feed1.run_time_sec);
        nvs_set_u32(h, "qtyG",   s_snap.feed1.quantity_g);
        nvs_set_u32(h, "consG",  s_snap.feed1.consumption_per_run_g);
        nvs_set_u32(h, "speed",  s_snap.feed1.speed_percent);
        nvs_set_u8 (h, "enabled",s_snap.feed1.enabled);
        nvs_set_u32(h, "todayG", s_snap.today_g);
        nvs_set_u32(h, "lifeG",  s_snap.lifetime_g);
        nvs_set_u32(h, "remG",   s_snap.remaining_g);
        nvs_set_str(h, "history", s_snap.history);
        esp_err_t e = nvs_commit(h);
        if (e != ESP_OK) ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(e));
        nvs_close(h);
    } else {
        ESP_LOGE(TAG, "nvs_open failed");
    }
    xSemaphoreGive(s_nvs_mutex);
}

void storage_reset(void)
{
    xSemaphoreTake(s_nvs_mutex, portMAX_DELAY);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreGive(s_nvs_mutex);

    STATE_LOCK();
    load_defaults_locked();
    STATE_UNLOCK();
}