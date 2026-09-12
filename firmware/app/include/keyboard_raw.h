#ifndef KEYBOARD_RAW_H
#define KEYBOARD_RAW_H
#include "keyboard_engine.h"
#include "keyboard_limits.h"
#define RAW_KEY_COUNT MT_KEY_CAPACITY
#define RAW_DEFAULT_PRESS 3500u
#define RAW_DEFAULT_RELEASE 3600u
/* Velocity window: the triggering sample plus up to nine more. A sample
 * below the bottom-out threshold closes the window early (it is excluded),
 * so very fast presses fit on as few as two readbacks. */
#define RAW_BOTTOM_OUT 1500u
#define RAW_VELOCITY_WINDOW 10u
typedef struct {
    float value;              /* clamp(raw counts/s / 4500000, 0, 1) */
    uint32_t captures;         /* completed fits, wrapping uint32 */
    uint16_t window[RAW_VELOCITY_WINDOW]; /* current fit samples, per key only */
    uint8_t count, pending;    /* samples collected; 1 while a fit is in flight */
    bool ready, valid;         /* release-observed arming; result available */
} keyboard_velocity_t;
typedef struct {
    keyboard_engine_t engine;
    uint16_t raw[RAW_KEY_COUNT], press[RAW_KEY_COUNT], release[RAW_KEY_COUNT];
    bool down[RAW_KEY_COUNT];
    uint8_t count, profile;
    bool enabled, armed, valid, midi_mode;
    bool menu_managed;
    uint32_t revision;
    keyboard_velocity_t velocity[RAW_KEY_COUNT];
} keyboard_raw_t;
void keyboard_raw_init(keyboard_raw_t *s);
void keyboard_raw_invalidate(keyboard_raw_t *s);
void keyboard_raw_enable(keyboard_raw_t *s, bool enabled);
bool keyboard_raw_set(keyboard_raw_t *s, unsigned index, unsigned press, unsigned release);
bool keyboard_raw_set_all(keyboard_raw_t *s, unsigned press, unsigned release);
/* MIDI-mode trigger point: level 1..10 selects a raw press threshold between
 * RAW_DEFAULT_PRESS and RAW_BOTTOM_OUT; per-key release values are kept and
 * the pair stays valid (press < release). */
unsigned keyboard_raw_press_level(unsigned level);
bool keyboard_raw_set_press_all(keyboard_raw_t *s, unsigned press);
void keyboard_raw_frame(keyboard_raw_t *s, const uint16_t *raw, uint8_t count,
                        uint8_t profile, bool valid);
#endif
