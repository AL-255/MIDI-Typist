#ifndef MIDI_TYPIST_KEYBOARD_LAYOUT_H
#define MIDI_TYPIST_KEYBOARD_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>
#include "keyboard_limits.h"
#include "keyboard_config.h"

/* Key IDs and layout IDs are opaque board-local handles, not HID usages or
 * sensor indices. The application never infers a physical position from them.
 * Action type 2 carries modifier bits + HID usage; type 0x11 carries a
 * configuration action. The board may translate any native mapping to this
 * representation without exposing its protocol or tables to the application. */
typedef struct {
    uint8_t key, type, length, arg0, arg1, arg2, arg3, arg4;
} keyboard_action_t;
typedef struct {
    uint8_t count, fn, tab, caps, escape;
    uint32_t sample_hz;
    const uint16_t *actuation_levels, *rapid_levels; /* eleven entries, 1..10 used */
    /* Board config/keymap.def: 256 entries indexed by opaque physical key ID,
     * not sensor index or HID usage. Fn routing never consults this map. */
    const uint8_t *keymap;
} keyboard_layout_t;

/* Immutable board description; NULL means unknown/unavailable layout.
 * count must fit MT_KEY_CAPACITY. Samples are canonical 1..4096, decreasing
 * with travel, delivered once per acquisition at sample_hz. Native ADC
 * direction/range and transport framing belong to the board port. */
const keyboard_layout_t *keyboard_layout(uint8_t profile);
uint8_t keyboard_key_for_sensor(uint8_t profile, uint8_t sensor);
const keyboard_action_t *keyboard_action(uint8_t profile, uint8_t key, uint8_t fn);
bool keyboard_lower_group(uint8_t profile, uint8_t key);
uint8_t keyboard_editor_digit(uint8_t profile, uint8_t key);
int keyboard_editor_step(uint8_t profile, uint8_t key);
bool keyboard_editor_preview_control(uint8_t profile, uint8_t key);
void keyboard_actuation_pair(const keyboard_config_t *config, uint8_t key,
                             uint8_t *press, uint8_t *release);
uint8_t keyboard_travel_level(uint16_t lower, uint16_t upper, uint16_t raw);

static inline unsigned keyboard_layout_count(uint8_t profile)
{
    const keyboard_layout_t *layout=keyboard_layout(profile);
    return layout && layout->sample_hz && layout->actuation_levels && layout->rapid_levels && layout->keymap &&
           layout->count<=MT_KEY_CAPACITY ? layout->count : 0u;
}
static inline bool keyboard_layout_valid(uint8_t profile, unsigned count)
{
    return count && count==keyboard_layout_count(profile);
}
#endif
