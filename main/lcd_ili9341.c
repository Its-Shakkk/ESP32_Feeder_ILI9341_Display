#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "font5x7.h"
#include "lcd_ili9341.h"

static const char *TAG = "lcd";

#define LCD_BUF_PIXELS  2048        // 4 KB DMA buffer (biggest glyph cell = 24x32 = 768 px)

static spi_device_handle_t s_spi;
static uint16_t *s_buf;             // DMA-capable pixel buffer

static inline uint16_t swap16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

// DC line is driven from the SPI pre-transfer callback (trans->user = DC level)
static void IRAM_ATTR lcd_pre_cb(spi_transaction_t *t)
{
    gpio_set_level(PIN_TFT_DC, (int)(intptr_t)t->user);
}

static void lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .flags = SPI_TRANS_USE_TXDATA,
        .tx_data = { cmd },
        .user = (void *)0,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void lcd_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .user = (void *)1,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void lcd_cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    lcd_cmd(cmd);
    lcd_data(data, len);
}

static void lcd_set_window(int x0, int y0, int x1, int y1)
{
    uint8_t col[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t row[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    lcd_cmd_data(0x2A, col, 4);
    lcd_cmd_data(0x2B, row, 4);
    lcd_cmd(0x2C);
}

static void lcd_send_pixels(const uint16_t *px, size_t n)
{
    lcd_data((const uint8_t *)px, n * 2);
}

void lcd_init(void)
{
    // Control GPIOs
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_TFT_DC) | (1ULL << PIN_TFT_RST) | (1ULL << PIN_TFT_BLK),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PIN_TFT_BLK, 0);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_TFT_MOSI,
        .miso_io_num = PIN_TFT_MISO,
        .sclk_io_num = PIN_TFT_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_BUF_PIXELS * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = LCD_SPI_HZ,
        .mode = 0,
        .spics_io_num = PIN_TFT_CS,
        .queue_size = 4,
        .pre_cb = lcd_pre_cb,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &dev, &s_spi));

    s_buf = heap_caps_malloc(LCD_BUF_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
    configASSERT(s_buf);

    // Hardware reset
    gpio_set_level(PIN_TFT_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TFT_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_TFT_RST, 1); vTaskDelay(pdMS_TO_TICKS(150));

    // Init sequence (same as Adafruit_ILI9341)
    lcd_cmd_data(0xEF, (const uint8_t[]){0x03, 0x80, 0x02}, 3);
    lcd_cmd_data(0xCF, (const uint8_t[]){0x00, 0xC1, 0x30}, 3);
    lcd_cmd_data(0xED, (const uint8_t[]){0x64, 0x03, 0x12, 0x81}, 4);
    lcd_cmd_data(0xE8, (const uint8_t[]){0x85, 0x00, 0x78}, 3);
    lcd_cmd_data(0xCB, (const uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5);
    lcd_cmd_data(0xF7, (const uint8_t[]){0x20}, 1);
    lcd_cmd_data(0xEA, (const uint8_t[]){0x00, 0x00}, 2);
    lcd_cmd_data(0xC0, (const uint8_t[]){0x23}, 1);              // Power control 1
    lcd_cmd_data(0xC1, (const uint8_t[]){0x10}, 1);              // Power control 2
    lcd_cmd_data(0xC5, (const uint8_t[]){0x3E, 0x28}, 2);        // VCOM 1
    lcd_cmd_data(0xC7, (const uint8_t[]){0x86}, 1);              // VCOM 2
    lcd_cmd_data(0x36, (const uint8_t[]){LCD_MADCTL}, 1);        // Landscape
    lcd_cmd_data(0x37, (const uint8_t[]){0x00}, 1);              // Vertical scroll start
    lcd_cmd_data(0x3A, (const uint8_t[]){0x55}, 1);              // 16 bit / pixel
    lcd_cmd_data(0xB1, (const uint8_t[]){0x00, 0x18}, 2);        // Frame rate
    lcd_cmd_data(0xB6, (const uint8_t[]){0x08, 0x82, 0x27}, 3);  // Display function
    lcd_cmd_data(0xF2, (const uint8_t[]){0x00}, 1);              // 3Gamma off
    lcd_cmd_data(0x26, (const uint8_t[]){0x01}, 1);              // Gamma curve
    lcd_cmd_data(0xE0, (const uint8_t[]){0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,
                                         0x37,0x07,0x10,0x03,0x0E,0x09,0x00}, 15);
    lcd_cmd_data(0xE1, (const uint8_t[]){0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,
                                         0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F}, 15);
    lcd_cmd(0x11);                                               // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(0x29);                                               // Display on
    vTaskDelay(pdMS_TO_TICKS(20));

    lcd_fill_screen(COLOR_BLACK);
    gpio_set_level(PIN_TFT_BLK, 1);
    ESP_LOGI(TAG, "ILI9341 ready (%dx%d)", LCD_H_RES, LCD_V_RES);
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    // clip
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H_RES) w = LCD_H_RES - x;
    if (y + h > LCD_V_RES) h = LCD_V_RES - y;
    if (w <= 0 || h <= 0) return;

    size_t total = (size_t)w * h;
    size_t chunk = total < LCD_BUF_PIXELS ? total : LCD_BUF_PIXELS;
    uint16_t c = swap16(color);
    for (size_t i = 0; i < chunk; i++) s_buf[i] = c;

    lcd_set_window(x, y, x + w - 1, y + h - 1);
    while (total) {
        size_t n = total < chunk ? total : chunk;
        lcd_send_pixels(s_buf, n);
        total -= n;
    }
}

void lcd_fill_screen(uint16_t color) { lcd_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, color); }
void lcd_hline(int x, int y, int w, uint16_t color) { lcd_fill_rect(x, y, w, 1, color); }

void lcd_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    lcd_fill_rect(x, y, w, 1, color);
    lcd_fill_rect(x, y + h - 1, w, 1, color);
    lcd_fill_rect(x, y, 1, h, color);
    lcd_fill_rect(x + w - 1, y, 1, h, color);
}

void lcd_draw_char(int x, int y, char c, uint8_t size, uint16_t fg, uint16_t bg)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *g = font5x7[c - 32];
    int w = 6 * size, h = 8 * size;
    if (x < 0 || y < 0 || x + w > LCD_H_RES || y + h > LCD_V_RES) return;

    uint16_t f = swap16(fg), b = swap16(bg);
    for (int row = 0; row < h; row++) {
        int gy = row / size;
        for (int col = 0; col < w; col++) {
            int gx = col / size;
            bool on = (gx < 5) && ((g[gx] >> gy) & 1);
            s_buf[row * w + col] = on ? f : b;
        }
    }
    lcd_set_window(x, y, x + w - 1, y + h - 1);
    lcd_send_pixels(s_buf, (size_t)w * h);
}

void lcd_draw_text(int x, int y, uint8_t size, uint16_t fg, uint16_t bg, const char *str)
{
    for (; *str; str++, x += 6 * size) lcd_draw_char(x, y, *str, size, fg, bg);
}
