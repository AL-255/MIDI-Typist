#ifndef KEYBOARD_MIDI_H
#define KEYBOARD_MIDI_H
#include "keyboard_raw.h"
#include "midi_music.h"
#define MIDI_UNMAPPED 255u
#define MIDI_CLEANUP_EVENTS 133u
typedef bool (*midi_send_fn)(uint8_t, uint8_t, uint8_t, uint8_t);
typedef struct {
    uint8_t mapping[RAW_KEY_COUNT], role[RAW_KEY_COUNT], active[RAW_KEY_COUNT], current[RAW_KEY_COUNT], released[RAW_KEY_COUNT];
    uint8_t pending[RAW_KEY_COUNT][MIDI_PENDING_STRIKES];
    uint8_t pending_mask[RAW_KEY_COUNT]; /* occupied strike slots; zero is the common case */
    /* Physical controller list built once per layout, including duplicate
     * roles. Avoid full-keyboard searches for each controller every scan. */
    uint8_t controls[RAW_KEY_COUNT], control_count;
    uint32_t note_work[MT_KEY_BITMAP_WORDS]; /* edges, delayed strikes and active voices */
    bool previous[RAW_KEY_COUNT];
    uint8_t refs[128], pressure[128], sent_pressure[128];
    uint8_t queue[MIDI_QUEUE][3];
    uint16_t head, count, panic;
    uint16_t bend, sent_bend;
    uint8_t modulation, sent_modulation, wheel_sweep;
    uint8_t profile, mode, pressure_cursor;
    uint8_t lower_rows[MT_KEY_BITMAP_BYTES]; /* board-described group, independent of mapping */
    bool lower_muted, sustain, janko;
    uint8_t velocity_start; /* 1..10: transmitted-velocity start, 1 = 0%, 10 = 100% */
    midi_music_config_t music;
    int8_t octave;
    bool was_armed, pressure_sweep, host_dirty;
    uint32_t changes, changed_at, errors, pressure_at;
    uint32_t wheel_at;
} keyboard_midi_t;
void keyboard_midi_init(keyboard_midi_t *s);
void keyboard_midi_abort(keyboard_midi_t *s);
void keyboard_midi_toggle(keyboard_midi_t *s, keyboard_raw_t *raw, uint32_t now);
void keyboard_midi_toggle_lower(keyboard_midi_t *s, keyboard_raw_t *raw);
void keyboard_midi_toggle_janko(keyboard_midi_t *s, keyboard_raw_t *raw);
void keyboard_midi_set_velocity_start(keyboard_midi_t *s, unsigned level);
bool keyboard_midi_select_music(keyboard_midi_t *s, keyboard_raw_t *raw, unsigned root, unsigned scale);
void keyboard_midi_guard(keyboard_midi_t *s, keyboard_raw_t *raw);
void keyboard_midi_frame(keyboard_midi_t *s, keyboard_raw_t *raw,
                         const uint16_t *lower, const uint16_t *upper, uint32_t now);
bool keyboard_midi_map(keyboard_midi_t *s, keyboard_raw_t *raw, unsigned sensor, unsigned note);
void keyboard_midi_service(keyboard_midi_t *s, uint32_t now, midi_send_fn send);
void keyboard_midi_lights(const keyboard_midi_t *s, uint8_t *frame, uint32_t now);
#endif
