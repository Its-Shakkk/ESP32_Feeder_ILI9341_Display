#pragma once
#include <stdint.h>
#include <stdbool.h>

// RGB565 colours (same values as Adafruit_ILI9341)
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_CYAN    0x07FF
#define COLOR_GREEN   0x07E0
#define COLOR_YELLOW  0xFFE0
#define COLOR_RED     0xF800
#define COLOR_GREY    0x2104

// NOTE: not thread-safe on purpose - only display_task draws.

void lcd_init(void);
void lcd_fill_screen(uint16_t color);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);
void lcd_hline(int x, int y, int w, uint16_t color);
void lcd_draw_rect(int x, int y, int w, int h, uint16_t color);

// Draws one 6x8 (x size) character cell with an opaque background.
void lcd_draw_char(int x, int y, char c, uint8_t size, uint16_t fg, uint16_t bg);
void lcd_draw_text(int x, int y, uint8_t size, uint16_t fg, uint16_t bg, const char *str);
