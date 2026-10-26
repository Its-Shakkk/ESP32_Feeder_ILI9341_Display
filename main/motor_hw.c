#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "app_config.h"
#include "motor_hw.h"

static const char *TAG = "motor";

#define M2_MODE     LEDC_LOW_SPEED_MODE
#define M2_TIMER    LEDC_TIMER_0
#define M2_CHANNEL  LEDC_CHANNEL_0

static SemaphoreHandle_t s_mutex;
static esp_timer_handle_t s_shutdown_timer;

static void pwm_write_locked(int duty)
{
    ledc_set_duty(M2_MODE, M2_CHANNEL, duty);
    ledc_update_duty(M2_MODE, M2_CHANNEL);
}

static int percent_to_duty(uint8_t pct)
{
    if (pct > 100) pct = 100;
    return (pct * 255) / 100;       // same as Arduino map(pct,0,100,0,255)
}

static void shutdown_cb(void *arg)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    pwm_write_locked(0);
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Motor2 (PWM) OFF.");
}

void motor_hw_init(void)
{
    s_mutex = xSemaphoreCreateMutex();

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_MOTOR1,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PIN_MOTOR1, 0);

    ledc_timer_config_t tcfg = {
        .speed_mode = M2_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = M2_TIMER,
        .freq_hz = MOTOR2_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));

    ledc_channel_config_t ccfg = {
        .gpio_num = PIN_MOTOR2,
        .speed_mode = M2_MODE,
        .channel = M2_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = M2_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));

    esp_timer_create_args_t targs = { .callback = shutdown_cb, .name = "m2_off" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_shutdown_timer));
}

void motor_start(uint8_t speed_percent)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_timer_stop(s_shutdown_timer);           // cancel pending shutdown (ignore error)
    gpio_set_level(PIN_MOTOR1, 1);
    int duty = percent_to_duty(speed_percent);
    pwm_write_locked(duty);
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Motors STARTED: Relay=ON, Motor2 PWM=%d%% (duty=%d)", speed_percent, duty);
}

void motor_stop(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    gpio_set_level(PIN_MOTOR1, 0);
    esp_timer_stop(s_shutdown_timer);
    esp_timer_start_once(s_shutdown_timer, (uint64_t)MOTOR2_SHUTDOWN_DELAY_MS * 1000);
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Motor1 (Relay) OFF. Motor2 (PWM) will stop in %d ms...", MOTOR2_SHUTDOWN_DELAY_MS);
}

void motor_set_speed(uint8_t speed_percent)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int duty = percent_to_duty(speed_percent);
    pwm_write_locked(duty);
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Live PWM update: %d%% (duty=%d)", speed_percent, duty);
}

void motor_all_off(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_timer_stop(s_shutdown_timer);
    gpio_set_level(PIN_MOTOR1, 0);
    pwm_write_locked(0);
    xSemaphoreGive(s_mutex);
}