#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "device_state.h"
#include "lcd_ili9341.h"
#include "motor_hw.h"

static const char *TAG = "ui";

// --- Colors ---
#define BG_COLOR      COLOR_BLACK
#define BORDER_COLOR  COLOR_WHITE
#define TEXT_WHITE    COLOR_WHITE
#define TEXT_CYAN     COLOR_CYAN
#define TEXT_GREEN    COLOR_GREEN
#define TEXT_YELLOW   COLOR_YELLOW
#define TEXT_RED      COLOR_RED
#define TRACK_GREY    COLOR_GREY

typedef enum {
    UI_HOME, UI_MENU, UI_MOTOR_SPEED, UI_ADD_FEED,
    UI_TOTAL_CONSUMPTION, UI_HISTORY, UI_FACTORY_RESET
} ui_state_t;

typedef enum { EDIT_MIN, EDIT_SEC } edit_mode_t;

static ui_state_t  s_ui = UI_HOME;
static edit_mode_t s_edit_mode = EDIT_MIN;

static const char *menu_items[] = { "Motor Speed", "Add Feed", "Total Consumption", "History", "Factory Reset" };
#define MENU_ITEMS_COUNT 5
static int s_menu_cursor, s_reset_cursor;

// Add Feed temp values
static int s_add_field, s_temp_qty_g, s_temp_run_time, s_temp_consum_g;
// Motor speed temp values
static int s_temp_speed = 50;
static char s_last_speed_str[10] = "";
static int  s_last_fill_w = -1;

// Partial update trackers
static float s_last_battery = -1.0f;
static int   s_last_wifi = -1;
static int   s_last_motor_state = -1;
static int   s_last_empty = -1;
static char  s_last_int_min[10], s_last_int_sec[10], s_last_run[15];
static char  s_last_qty[15], s_last_cons[15], s_last_rpm[15];
static char  s_last_add_qty[15], s_last_add_time[15], s_last_add_cons[15];

// =========================================================================
// DRAWING HELPERS
// =========================================================================
// grams -> "12.3 kg" (rounded to 0.1 kg)
static void fmt_kg(char *buf, size_t n, uint32_t g)
{
    uint32_t tenths = (g + 50) / 100;
    snprintf(buf, n, "%lu.%lu kg", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
}

static void put(int x, int y, uint8_t size, uint16_t color, const char *s)
{
    lcd_draw_text(x, y, size, color, BG_COLOR, s);
}

static void draw_value(int x, int y, uint8_t size, int clear_chars, const char *str, uint16_t color)
{
    lcd_fill_rect(x, y, clear_chars * 6 * size, 8 * size, BG_COLOR);
    put(x, y, size, color, str);
}

static void draw_text_diff_exact(int x, int y, uint8_t size, const char *old_s, const char *new_s, uint16_t color)
{
    int old_len = strlen(old_s), new_len = strlen(new_s);
    if (old_len != new_len) {
        draw_value(x, y, size, old_len > new_len ? old_len : new_len, new_s, color);
        return;
    }
    int cw = 6 * size;
    for (int i = 0; i < new_len; i++) {
        if (old_s[i] != new_s[i]) lcd_draw_char(x + i * cw, y, new_s[i], size, color, BG_COLOR);
    }
}

// =========================================================================
// HEADER
// =========================================================================
static void draw_header_wifi(bool connected)
{
    lcd_fill_rect(5, 10, 60, 12, BG_COLOR);
    if (connected) put(10, 10, 1, TEXT_GREEN, "ONLINE");
    else           put(10, 10, 1, TEXT_RED, "OFFLINE");
    s_last_wifi = connected;
}

static void draw_header_battery(float pct)
{
    lcd_fill_rect(225, 5, 90, 20, BG_COLOR);
    int bx = 280, by = 8;
    lcd_draw_rect(bx, by, 24, 14, TEXT_WHITE);
    lcd_fill_rect(bx + 24, by + 4, 3, 6, TEXT_WHITE);
    lcd_fill_rect(bx + 2, by + 2, (int)((pct / 100.0f) * 20), 10, TEXT_GREEN);
    char s[8];
    snprintf(s, sizeof(s), "%d%%", (int)pct);
    put(245, 12, 1, TEXT_WHITE, s);
    s_last_battery = pct;
}

static void draw_header_full(const device_state_t *s)
{
    lcd_fill_rect(0, 0, 320, 30, BG_COLOR);
    lcd_hline(0, 30, 320, BORDER_COLOR);
    s_last_battery = -1.0f;
    draw_header_wifi(s->wifi_connected);
    draw_header_battery(s->battery_percent);
}

// =========================================================================
// HOME SCREEN
// =========================================================================
#define HOME_VAL_X    185
#define HOME_SMALL_X  185

static void draw_home_static(const device_state_t *s)
{
    lcd_fill_screen(BG_COLOR);
    draw_header_full(s);

    put(10, 42,  2, TEXT_YELLOW, "FEED INTERVAL");
    put(10, 67,  2, TEXT_YELLOW, "RUN TIME");
    put(10, 92,  2, TEXT_YELLOW, "MOTOR");
    put(10, 117, 2, TEXT_YELLOW, "FEED QTY");
    put(10, 142, 2, TEXT_YELLOW, "CONSUM/FEED");
    put(10, 167, 2, TEXT_YELLOW, "MOTOR RPM");

    for (int y = 42; y <= 167; y += 25) put(175, y, 2, TEXT_CYAN, ":");

    lcd_hline(0, 200, 320, BORDER_COLOR);

    s_last_int_min[0] = s_last_int_sec[0] = s_last_run[0] = 0;
    s_last_qty[0] = s_last_cons[0] = s_last_rpm[0] = 0;
    s_last_motor_state = -1;
    s_last_empty = -1;          // footer is drawn by update_home_dynamic()
}

static void update_home_dynamic(const device_state_t *s, bool force)
{
    if (s_ui != UI_HOME) return;

    // header (partial updates)
    if (s->wifi_connected != s_last_wifi) draw_header_wifi(s->wifi_connected);
    if (s->battery_percent != s_last_battery) draw_header_battery(s->battery_percent);

    // footer: big red warning while the hopper is empty, otherwise the menu hint
    bool empty = (s->remaining_g == 0);
    if (force || (int)empty != s_last_empty) {
        lcd_fill_rect(0, 201, 320, 39, BG_COLOR);
        if (empty) {
            put(61, 204, 3, TEXT_RED, "REFILL FEED");
            put(106, 230, 1, TEXT_CYAN, "Hold OK > Add Feed");
        } else {
            put(100, 215, 1, TEXT_CYAN, "Hold OK for Menu");
        }
        s_last_empty = empty;
    }

    // FEED INTERVAL  00m 00s
    char min_buf[8], sec_buf[8];
    snprintf(min_buf, sizeof(min_buf), "%02dm", s->feed1.interval_min);
    snprintf(sec_buf, sizeof(sec_buf), "%02ds", s->feed1.interval_sec);
    bool editing = !s->motor_on;
    uint16_t min_col = (editing && s_edit_mode == EDIT_MIN) ? TEXT_YELLOW : TEXT_WHITE;
    uint16_t sec_col = (editing && s_edit_mode == EDIT_SEC) ? TEXT_YELLOW : TEXT_WHITE;

    if (force || strcmp(min_buf, s_last_int_min) != 0) {
        draw_value(HOME_VAL_X, 42, 2, 3, min_buf, min_col);
        strcpy(s_last_int_min, min_buf);
    }
    if (force || strcmp(sec_buf, s_last_int_sec) != 0) {
        draw_value(HOME_VAL_X + 48, 42, 2, 3, sec_buf, sec_col);
        strcpy(s_last_int_sec, sec_buf);
    }

    // RUN TIME  00 sec
    char run_buf[15];
    snprintf(run_buf, sizeof(run_buf), "%02d sec", (int)s->run_time_remaining_sec);
    if (force || strcmp(run_buf, s_last_run) != 0) {
        draw_text_diff_exact(HOME_VAL_X, 67, 2, s_last_run, run_buf, TEXT_WHITE);
        strcpy(s_last_run, run_buf);
    }

    // MOTOR ON/OFF
    if (force || (int)s->motor_on != s_last_motor_state) {
        draw_value(HOME_SMALL_X, 92, 2, 4, s->motor_on ? "ON" : "OFF", s->motor_on ? TEXT_GREEN : TEXT_RED);
        s_last_motor_state = s->motor_on;
    }

    // FEED QTY
    char qty_buf[15];
    fmt_kg(qty_buf, sizeof(qty_buf), s->feed1.quantity_g);
    if (force || strcmp(qty_buf, s_last_qty) != 0) {
        draw_value(HOME_SMALL_X, 117, 2, 10, qty_buf, TEXT_WHITE);
        strcpy(s_last_qty, qty_buf);
    }

    // CONSUM/FEED  (kg, 0.1 resolution)
    char cons_buf[15];
    fmt_kg(cons_buf, sizeof(cons_buf), s->feed1.consumption_per_run_g);
    if (force || strcmp(cons_buf, s_last_cons) != 0) {
        draw_value(HOME_SMALL_X, 142, 2, 11, cons_buf, TEXT_WHITE);
        strcpy(s_last_cons, cons_buf);
    }

    // MOTOR RPM (configured speed %)
    char rpm_buf[10];
    snprintf(rpm_buf, sizeof(rpm_buf), "%d %%", s->feed1.speed_percent);
    if (force || strcmp(rpm_buf, s_last_rpm) != 0) {
        draw_value(HOME_SMALL_X, 167, 2, 6, rpm_buf, TEXT_WHITE);
        strcpy(s_last_rpm, rpm_buf);
    }
}

static void home_refresh(bool force)
{
    device_state_t s;
    device_state_snapshot(&s);
    update_home_dynamic(&s, force);
}

static void show_home(void)
{
    device_state_t s;
    device_state_snapshot(&s);
    s_ui = UI_HOME;
    draw_home_static(&s);
    update_home_dynamic(&s, true);
}

// =========================================================================
// MENU
// =========================================================================
static void draw_menu_row(int index, bool selected)
{
    int start_y = 55, step_y = 20, y = start_y + index * step_y;
    lcd_fill_rect(0, y - 2, 320, step_y, BG_COLOR);
    lcd_draw_text(10, y, 1, selected ? TEXT_GREEN : TEXT_WHITE, BG_COLOR, selected ? "> " : "  ");
    lcd_draw_text(10 + 12, y, 1, selected ? TEXT_GREEN : TEXT_WHITE, BG_COLOR, menu_items[index]);
}

static void draw_menu_screen(void)
{
    lcd_fill_screen(BG_COLOR);
    put(120, 10, 2, TEXT_WHITE, "MENU");
    lcd_hline(0, 35, 320, BORDER_COLOR);
    for (int i = 0; i < MENU_ITEMS_COUNT; i++) draw_menu_row(i, i == s_menu_cursor);
    lcd_hline(0, 190, 320, BORDER_COLOR);
    put(10, 205, 1, TEXT_WHITE, "UP/DOWN: Select  OK: Enter");
    put(10, 220, 1, TEXT_WHITE, "LEFT: Back");
}

static void show_menu(void)
{
    s_ui = UI_MENU;
    draw_menu_screen();
}

// =========================================================================
// MOTOR SPEED SCREEN
// =========================================================================
#define BAR_X 40
#define BAR_Y 130
#define BAR_W 240
#define BAR_H 16

static void draw_motor_speed_static(void)
{
    lcd_fill_screen(BG_COLOR);
    put(85, 10, 2, TEXT_WHITE, "MOTOR SPEED");
    lcd_hline(0, 35, 320, BORDER_COLOR);
    put(BAR_X, BAR_Y + BAR_H + 6, 1, TEXT_WHITE, "MIN 10%");
    put(BAR_X + BAR_W - 48, BAR_Y + BAR_H + 6, 1, TEXT_WHITE, "MAX 100%");
    lcd_hline(0, 190, 320, BORDER_COLOR);
    put(10, 205, 1, TEXT_WHITE, "UP/DOWN: Adjust");
    put(10, 220, 1, TEXT_WHITE, "OK: Save     LEFT: Cancel");

    lcd_draw_rect(BAR_X - 2, BAR_Y - 2, BAR_W + 4, BAR_H + 4, TEXT_WHITE);
    lcd_fill_rect(BAR_X, BAR_Y, BAR_W, BAR_H, TRACK_GREY);
    s_last_speed_str[0] = 0;
    s_last_fill_w = 0;
}

static void update_motor_speed_dynamic(bool force)
{
    char ns[10];
    snprintf(ns, sizeof(ns), "%d %%", s_temp_speed);
    if (force || strcmp(ns, s_last_speed_str) != 0) {
        draw_text_diff_exact(110, 70, 4, s_last_speed_str, ns, TEXT_CYAN);
        strcpy(s_last_speed_str, ns);
    }

    int new_fill = (s_temp_speed - 10) * BAR_W / 90;
    if (force) {
        lcd_fill_rect(BAR_X, BAR_Y, BAR_W, BAR_H, TRACK_GREY);
        s_last_fill_w = 0;
    }
    if (new_fill > s_last_fill_w)
        lcd_fill_rect(BAR_X + s_last_fill_w, BAR_Y, new_fill - s_last_fill_w, BAR_H, TEXT_CYAN);
    else if (new_fill < s_last_fill_w)
        lcd_fill_rect(BAR_X + new_fill, BAR_Y, s_last_fill_w - new_fill, BAR_H, TRACK_GREY);
    s_last_fill_w = new_fill;
}

// =========================================================================
// ADD FEED
// =========================================================================
static void draw_add_feed_static(void)
{
    lcd_fill_screen(BG_COLOR);
    put(90, 10, 2, TEXT_WHITE, "ADD FEED");
    lcd_hline(0, 35, 320, BORDER_COLOR);

    put(20, 60,  1, TEXT_CYAN, "Quantity (kg):");
    put(20, 90,  1, TEXT_CYAN, "Run Time (sec):");
    put(20, 120, 1, TEXT_CYAN, "Consum (kg/run):");

    put(20, 170, 1, TEXT_WHITE, "RIGHT: Next Field");
    put(20, 190, 1, TEXT_WHITE, "UP/DOWN: Adjust Value");
    lcd_hline(0, 210, 320, BORDER_COLOR);
    put(10, 225, 1, TEXT_WHITE, "OK/LEFT: Save & Back");
    s_last_add_qty[0] = s_last_add_time[0] = s_last_add_cons[0] = 0;
}

static void update_add_feed_dynamic(bool force)
{
    char b[16];
    fmt_kg(b, sizeof(b), (uint32_t)s_temp_qty_g);
    if (force || strcmp(b, s_last_add_qty) != 0) {
        draw_value(160, 60, 1, 12, b, s_add_field == 0 ? TEXT_YELLOW : TEXT_WHITE);
        strcpy(s_last_add_qty, b);
    }
    snprintf(b, sizeof(b), "%d sec", s_temp_run_time);
    if (force || strcmp(b, s_last_add_time) != 0) {
        draw_value(160, 90, 1, 12, b, s_add_field == 1 ? TEXT_YELLOW : TEXT_WHITE);
        strcpy(s_last_add_time, b);
    }
    fmt_kg(b, sizeof(b), (uint32_t)s_temp_consum_g);
    if (force || strcmp(b, s_last_add_cons) != 0) {
        draw_value(160, 120, 1, 12, b, s_add_field == 2 ? TEXT_YELLOW : TEXT_WHITE);
        strcpy(s_last_add_cons, b);
    }
}

// =========================================================================
// TOTAL CONSUMPTION / HISTORY / FACTORY RESET
// =========================================================================
static void draw_total_consumption_screen(void)
{
    device_state_t s;
    device_state_snapshot(&s);
    char b[24];

    lcd_fill_screen(BG_COLOR);
    put(40, 10, 2, TEXT_WHITE, "TOTAL CONSUMPTION");
    lcd_hline(0, 35, 320, BORDER_COLOR);

    put(20, 60, 1, TEXT_CYAN, "Today:");
    fmt_kg(b, sizeof(b), s.today_g);
    put(120, 60, 1, TEXT_WHITE, b);

    put(20, 90, 1, TEXT_CYAN, "Lifetime:");
    fmt_kg(b, sizeof(b), s.lifetime_g);
    put(120, 90, 1, TEXT_WHITE, b);

    put(20, 120, 1, TEXT_CYAN, "Remaining:");
    fmt_kg(b, sizeof(b), s.remaining_g);
    put(120, 120, 1, TEXT_WHITE, b);

    put(10, 220, 1, TEXT_WHITE, "LEFT: Back");
}

static void draw_history_screen(void)
{
    device_state_t s;
    device_state_snapshot(&s);

    lcd_fill_screen(BG_COLOR);
    put(110, 10, 2, TEXT_WHITE, "HISTORY");
    lcd_hline(0, 35, 320, BORDER_COLOR);

    int start_y = 55, step_y = 18, index = 0;
    const char *p = s.history;
    while (*p && index < 5) {
        const char *comma = strchr(p, ',');
        if (!comma) break;
        const char *colon = memchr(p, ':', comma - p);
        if (colon) {
            char date[16], val[16];
            size_t dl = colon - p, vl = comma - colon - 1;
            if (dl >= sizeof(date)) dl = sizeof(date) - 1;
            if (vl >= sizeof(val))  vl = sizeof(val) - 1;
            memcpy(date, p, dl); date[dl] = 0;
            memcpy(val, colon + 1, vl); val[vl] = 0;
            put(20, start_y + index * step_y, 1, TEXT_WHITE, date);
            char line[24];
            fmt_kg(line, sizeof(line), (uint32_t)strtoul(val, NULL, 10));
            put(150, start_y + index * step_y, 1, TEXT_WHITE, line);
            index++;
        }
        p = comma + 1;
    }
    if (index == 0) put(20, 80, 1, TEXT_WHITE, "No history yet.");
    put(10, 220, 1, TEXT_WHITE, "LEFT: Back");
}

static void draw_factory_reset_screen(void)
{
    lcd_fill_screen(BG_COLOR);
    put(60, 10, 2, TEXT_WHITE, "FACTORY RESET");
    lcd_hline(0, 35, 320, BORDER_COLOR);

    put(20, 80, 1, TEXT_WHITE, "Reset all settings and clear history?");

    lcd_draw_rect(40, 120, 80, 40, TEXT_WHITE);
    put(70, 135, 1, s_reset_cursor == 0 ? TEXT_RED : TEXT_WHITE, "NO");

    lcd_draw_rect(200, 120, 80, 40, TEXT_WHITE);
    put(225, 135, 1, s_reset_cursor == 1 ? TEXT_GREEN : TEXT_WHITE, "YES");

    lcd_hline(0, 190, 320, BORDER_COLOR);
    put(10, 205, 1, TEXT_WHITE, "LEFT/RIGHT: Select");
    put(10, 220, 1, TEXT_WHITE, "OK: Confirm");
}

// =========================================================================
// STATE MACHINE (one button event at a time)
// =========================================================================
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int wrap_add(int v, int d, int mod) { return ((v + d) % mod + mod) % mod; }

// Interval edits are written to NVS once, 1 s after the last change, so a held
// button (repeat every 500 ms) does not hammer the flash.
static bool      s_cfg_dirty;
static TickType_t s_last_edit_tick;
static void mark_dirty(void) { s_cfg_dirty = true; s_last_edit_tick = xTaskGetTickCount(); }

static void handle_home(button_event_t ev)
{
    if (ev == BTN_EVT_OK_SHORT) {
        bool start = false, stop = false;
        uint8_t speed;

        STATE_LOCK();
        feed_config_t *f = &g_dev.feed1;
        if (g_dev.configuring_interval) {
            g_dev.configuring_interval = false;
            f->run_time_sec = f->interval_sec;
            g_dev.next_feed_remaining_sec = (uint32_t)f->interval_min * 60;
            g_dev.run_time_remaining_sec = f->interval_sec;
            f->enabled = true;
            g_dev.motor_on = true;
            start = true;
        } else if (g_dev.motor_on) {
            g_dev.motor_on = false;
            stop = true;
            g_dev.configuring_interval = true;
            g_dev.run_time_remaining_sec = f->interval_sec;
        } else {
            g_dev.motor_on = true;
            g_dev.run_time_remaining_sec = f->interval_sec;
            g_dev.next_feed_remaining_sec = (uint32_t)f->interval_min * 60;
            g_dev.configuring_interval = false;
            f->enabled = true;
            start = true;
        }
        if (start && g_dev.remaining_g == 0) {
            // Hopper empty: the schedule is armed but the motor must not run.
            g_dev.motor_on = false;
            start = false;
        }
        speed = f->speed_percent;
        STATE_UNLOCK();

        if (stop)  motor_stop();
        if (start) motor_start(speed);
        storage_save();
        home_refresh(true);
        return;
    }

    if (ev == BTN_EVT_OK_LONG) {
        s_menu_cursor = 0;
        show_menu();
        return;
    }

    // editing is only possible while the motor is off
    bool motor_on;
    STATE_LOCK(); motor_on = g_dev.motor_on; STATE_UNLOCK();
    if (motor_on) return;

    if (ev == BTN_EVT_LEFT || ev == BTN_EVT_RIGHT) {
        s_edit_mode = (s_edit_mode == EDIT_MIN) ? EDIT_SEC : EDIT_MIN;
        home_refresh(true);
    } else if (ev == BTN_EVT_UP || ev == BTN_EVT_DOWN ||
               ev == BTN_EVT_UP_LONG || ev == BTN_EVT_DOWN_LONG) {
        bool up  = (ev == BTN_EVT_UP || ev == BTN_EVT_UP_LONG);
        int step = (ev == BTN_EVT_UP_LONG || ev == BTN_EVT_DOWN_LONG) ? 10 : 1;
        int d    = up ? step : -step;

        STATE_LOCK();
        feed_config_t *f = &g_dev.feed1;
        if (s_edit_mode == EDIT_MIN) f->interval_min = wrap_add(f->interval_min, d, 100);
        else                         f->interval_sec = wrap_add(f->interval_sec, d, 60);
        g_dev.next_feed_remaining_sec = (uint32_t)f->interval_min * 60;
        g_dev.run_time_remaining_sec = f->interval_sec;
        f->run_time_sec = f->interval_sec;
        STATE_UNLOCK();
        home_refresh(true);
        mark_dirty();
    }
}

static void handle_menu(button_event_t ev)
{
    if (ev == BTN_EVT_UP) {
        draw_menu_row(s_menu_cursor, false);
        if (--s_menu_cursor < 0) s_menu_cursor = MENU_ITEMS_COUNT - 1;
        draw_menu_row(s_menu_cursor, true);
    } else if (ev == BTN_EVT_DOWN) {
        draw_menu_row(s_menu_cursor, false);
        if (++s_menu_cursor >= MENU_ITEMS_COUNT) s_menu_cursor = 0;
        draw_menu_row(s_menu_cursor, true);
    } else if (ev == BTN_EVT_LEFT) {
        show_home();
    } else if (ev == BTN_EVT_OK_SHORT || ev == BTN_EVT_RIGHT) {
        device_state_t s;
        device_state_snapshot(&s);
        switch (s_menu_cursor) {
        case 0:
            s_temp_speed = s.feed1.speed_percent;
            if (s_temp_speed < 10) s_temp_speed = 10;
            s_ui = UI_MOTOR_SPEED;
            draw_motor_speed_static();
            update_motor_speed_dynamic(true);
            break;
        case 1:
            s_add_field = 0;
            s_temp_qty_g = (int)(((s.feed1.quantity_g + 50) / 100) * 100);
            s_temp_run_time = s.feed1.run_time_sec;
            s_temp_consum_g = (int)(((s.feed1.consumption_per_run_g + 50) / 100) * 100);
            s_ui = UI_ADD_FEED;
            draw_add_feed_static();
            update_add_feed_dynamic(true);
            break;
        case 2: s_ui = UI_TOTAL_CONSUMPTION; draw_total_consumption_screen(); break;
        case 3: s_ui = UI_HISTORY; draw_history_screen(); break;
        case 4: s_reset_cursor = 0; s_ui = UI_FACTORY_RESET; draw_factory_reset_screen(); break;
        }
    }
}

static void handle_add_feed(button_event_t ev)
{
    switch (ev) {
    case BTN_EVT_RIGHT:
        s_add_field = (s_add_field + 1) % 3;
        update_add_feed_dynamic(true);
        return;
    case BTN_EVT_UP:
        if (s_add_field == 0)      s_temp_qty_g   = clampi(s_temp_qty_g + 100, 0, FEED_QTY_MAX_KG * 1000);
        else if (s_add_field == 1) s_temp_run_time = clampi(s_temp_run_time + 1, 1, FEED_RUN_MAX_SEC);
        else                       s_temp_consum_g = clampi(s_temp_consum_g + 100, 0, FEED_CONSUM_MAX_G);
        update_add_feed_dynamic(false);
        return;
    case BTN_EVT_DOWN:
        if (s_add_field == 0)      s_temp_qty_g   = clampi(s_temp_qty_g - 100, 0, FEED_QTY_MAX_KG * 1000);
        else if (s_add_field == 1) s_temp_run_time = clampi(s_temp_run_time - 1, 1, FEED_RUN_MAX_SEC);
        else                       s_temp_consum_g = clampi(s_temp_consum_g - 100, 0, FEED_CONSUM_MAX_G);
        update_add_feed_dynamic(false);
        return;
    case BTN_EVT_UP_LONG:
        if (s_add_field == 0) s_temp_qty_g   = clampi(s_temp_qty_g + 10000, 0, FEED_QTY_MAX_KG * 1000);
        if (s_add_field == 1) s_temp_run_time = clampi(s_temp_run_time + 10, 1, FEED_RUN_MAX_SEC);
        if (s_add_field == 2) s_temp_consum_g = clampi(s_temp_consum_g + 1000, 0, FEED_CONSUM_MAX_G);
        update_add_feed_dynamic(false);
        return;
    case BTN_EVT_DOWN_LONG:
        if (s_add_field == 0) s_temp_qty_g   = clampi(s_temp_qty_g - 10000, 0, FEED_QTY_MAX_KG * 1000);
        if (s_add_field == 1) s_temp_run_time = clampi(s_temp_run_time - 10, 1, FEED_RUN_MAX_SEC);
        if (s_add_field == 2) s_temp_consum_g = clampi(s_temp_consum_g - 1000, 0, FEED_CONSUM_MAX_G);
        update_add_feed_dynamic(false);
        return;
    case BTN_EVT_LEFT:
    case BTN_EVT_OK_SHORT:
        STATE_LOCK();
        // OK = "feed added": hopper is set to the quantity shown, even if the number is unchanged.
        // LEFT = just go back: the hopper is only refilled if the quantity was edited.
        if (ev == BTN_EVT_OK_SHORT || g_dev.feed1.quantity_g != (uint32_t)s_temp_qty_g)
            g_dev.remaining_g = (uint32_t)s_temp_qty_g;
        g_dev.feed1.quantity_g = (uint32_t)s_temp_qty_g;
        g_dev.feed1.run_time_sec = s_temp_run_time;
        g_dev.feed1.consumption_per_run_g = s_temp_consum_g;
        STATE_UNLOCK();
        storage_save();
        show_menu();
        return;
    default:
        return;
    }
}

static void handle_motor_speed(button_event_t ev)
{
    if (ev == BTN_EVT_UP || ev == BTN_EVT_UP_LONG) {
        s_temp_speed = clampi(s_temp_speed + 10, 10, 100);
        update_motor_speed_dynamic(false);
    } else if (ev == BTN_EVT_DOWN || ev == BTN_EVT_DOWN_LONG) {
        s_temp_speed = clampi(s_temp_speed - 10, 10, 100);
        update_motor_speed_dynamic(false);
    } else if (ev == BTN_EVT_LEFT) {
        show_menu();
    } else if (ev == BTN_EVT_OK_SHORT || ev == BTN_EVT_RIGHT) {
        bool running;
        STATE_LOCK();
        g_dev.feed1.speed_percent = s_temp_speed;
        running = g_dev.motor_on;
        STATE_UNLOCK();
        if (running) motor_set_speed(s_temp_speed);
        storage_save();
        show_menu();
    }
}

static void handle_factory_reset(button_event_t ev)
{
    if (ev == BTN_EVT_LEFT || ev == BTN_EVT_RIGHT) {
        s_reset_cursor = (s_reset_cursor + 1) % 2;
        draw_factory_reset_screen();
    } else if (ev == BTN_EVT_OK_SHORT) {
        if (s_reset_cursor == 1) {
            motor_all_off();
            s_cfg_dirty = false;
            storage_reset();
            lcd_fill_screen(BG_COLOR);
            put(60, 100, 2, TEXT_RED, "RESETTING...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        } else {
            show_menu();
        }
    }
}

static void handle_event(button_event_t ev)
{
    switch (s_ui) {
    case UI_HOME:              handle_home(ev); break;
    case UI_MENU:              handle_menu(ev); break;
    case UI_ADD_FEED:          handle_add_feed(ev); break;
    case UI_MOTOR_SPEED:       handle_motor_speed(ev); break;
    case UI_TOTAL_CONSUMPTION:
    case UI_HISTORY:           if (ev == BTN_EVT_LEFT) show_menu(); break;
    case UI_FACTORY_RESET:     handle_factory_reset(ev); break;
    }
}

// =========================================================================
// TASK
// =========================================================================
void display_task(void *arg)
{
    show_home();
    ESP_LOGI(TAG, "display task running");

    for (;;) {
        button_event_t ev;
        // Blocks until a button event arrives, or 100 ms passes (periodic redraw).
        if (xQueueReceive(g_button_queue, &ev, pdMS_TO_TICKS(DISPLAY_TICK_MS)) == pdTRUE) {
            handle_event(ev);
        }
        if (s_cfg_dirty && (xTaskGetTickCount() - s_last_edit_tick) >= pdMS_TO_TICKS(1000)) {
            s_cfg_dirty = false;
            storage_save();
        }
        if (s_ui == UI_HOME) home_refresh(false);
    }
}