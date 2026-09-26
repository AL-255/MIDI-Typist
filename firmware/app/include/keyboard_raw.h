#ifndef KEYBOARD_RAW_H
#define KEYBOARD_RAW_H
#include "defaults.h"
#include "keyboard_engine.h"
#include "keyboard_limits.h"
#define RAW_KEY_COUNT MT_KEY_CAPACITY
/* Velocity window: the triggering sample plus up to nine more. A sample
 * below the bottom-out threshold closes the window early (it is excluded),
 * so very fast presses fit on as few as two readbacks. */
typedef struct {
    float value;              /* clamp(counts/s / VELOCITY_MAX_COUNTS_PER_SECOND, 0, 1) */
    uint32_t captures;         /* completed fits, wrapping uint32 */
    uint16_t window[RAW_VELOCITY_WINDOW]; /* current fit samples, per key only */
    uint8_t count, pending;    /* samples collected; 1 while a fit is in flight */
    bool ready, valid;         /* release-observed arming; result available */
} keyboard_velocity_t;
typedef struct {
    keyboard_engine_t engine;
    uint16_t raw[RAW_KEY_COUNT], press[RAW_KEY_COUNT], release[RAW_KEY_COUNT];
    bool down[RAW_KEY_COUNT];
    uint8_t count, profile, keymap_profile;
    bool enabled, armed, valid, midi_mode;
    bool menu_managed, neutral_idle;
    /* Per-frame edges plus unfinished/held velocity owners. Observation still
     * validates every sensor at the board's exact acquisition cadence. */
    uint8_t changed_keys[RAW_KEY_COUNT], changed_count;
    uint32_t nonneutral[MT_KEY_BITMAP_WORDS], velocity_work[MT_KEY_BITMAP_WORDS];
    uint32_t revision;
    keyboard_velocity_t velocity[RAW_KEY_COUNT];
    uint8_t keycode[RAW_KEY_COUNT]; /* base keyboard outputs; physical/Fn/MIDI unchanged */
} keyboard_raw_t;
void keyboard_raw_init(keyboard_raw_t *s);
void keyboard_raw_invalidate(keyboard_raw_t *s);
void keyboard_raw_enable(keyboard_raw_t *s, bool enabled);
bool keyboard_raw_set(keyboard_raw_t *s, unsigned index, unsigned press, unsigned release);
bool keyboard_raw_set_all(keyboard_raw_t *s, unsigned press, unsigned release);
bool keyboard_raw_map(keyboard_raw_t *s,unsigned sensor,unsigned usage);
/* MIDI-mode trigger point: level 1..10 selects a raw press threshold between
 * RAW_BOTTOM_OUT and RAW_DEFAULT_RELEASE-1; per-key release values are kept and
 * the pair stays valid (press < release). */
unsigned keyboard_raw_press_level(unsigned level);
bool keyboard_raw_set_press_all(keyboard_raw_t *s, unsigned press);
/* Menu/calibration observation: validate/copy every sample and update
 * neutral_idle without arming outputs, detecting edges or fitting velocity.
 * Invalidate before returning to normal frame processing. */
void keyboard_raw_observe(keyboard_raw_t *s, const uint16_t *raw, uint8_t count,
                          uint8_t profile, bool valid);
void keyboard_raw_frame(keyboard_raw_t *s, const uint16_t *raw, uint8_t count,
                        uint8_t profile, bool valid);
#endif
