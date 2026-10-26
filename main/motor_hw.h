#pragma once
#include <stdint.h>

// Thread-safe motor hardware helpers.
// Motor 1 = relay (ON/OFF). Motor 2 = PWM, switched off MOTOR2_SHUTDOWN_DELAY_MS
// after the relay (coast-down), exactly like the original sketch.
void motor_hw_init(void);
void motor_start(uint8_t speed_percent);
void motor_stop(void);
void motor_set_speed(uint8_t speed_percent);   // live PWM update while running
void motor_all_off(void);                      // immediate, no delay