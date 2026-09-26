#ifndef KEYBOARD_CALIBRATION_H
#define KEYBOARD_CALIBRATION_H
#include <stdbool.h>
#include <stdint.h>
#include "keyboard_limits.h"
#define CAL_KEYS MT_KEY_CAPACITY
enum cal_state { CAL_IDLE, CAL_RELEASE, CAL_SETTLE, CAL_COLLECT,
                 CAL_SAVE=5, /* wire value 4 is reserved */ CAL_DONE, CAL_ABORTED, CAL_ERROR };
enum cal_reason { CAL_OK, CAL_TIMEOUT, CAL_INVALID, CAL_CANCELLED, CAL_STORAGE };
typedef struct {
    uint32_t since, sum, samples;
    uint16_t anchor;
    bool active;
} calibration_hold_t;
typedef struct {
    uint16_t lower[CAL_KEYS], upper[CAL_KEYS];
    calibration_hold_t holds[CAL_KEYS];
    uint8_t done[MT_KEY_BITMAP_BYTES], state, reason, count, profile, selected, completed;
    uint32_t since, activity;
} keyboard_calibration_t;
void calibration_init(keyboard_calibration_t *s);
bool calibration_active(const keyboard_calibration_t *s);
bool calibration_start(keyboard_calibration_t *s, uint8_t profile, uint8_t count, uint32_t now);
void calibration_abort(keyboard_calibration_t *s, uint8_t reason, uint32_t now);
void calibration_frame(keyboard_calibration_t *s, const uint16_t *raw, bool valid, bool neutral, uint32_t now);
void calibration_tick(keyboard_calibration_t *s, bool healthy, uint32_t now);
void calibration_finish(keyboard_calibration_t *s, bool success, uint32_t now);
void calibration_lights(const keyboard_calibration_t *s, uint8_t *rgb, uint32_t now);
bool calibration_bounds_valid(uint8_t profile, uint8_t count, const uint16_t *lo, const uint16_t *hi);
unsigned calibration_min_span(uint8_t profile);
#endif
