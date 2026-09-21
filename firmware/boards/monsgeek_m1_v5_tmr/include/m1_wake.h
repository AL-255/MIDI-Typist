#ifndef MIDI_TYPIST_M1_WAKE_H
#define MIDI_TYPIST_M1_WAKE_H
#include "m1_board.h"

typedef enum { M1_WAKE_WAIT, M1_WAKE_KEYS, M1_WAKE_FAULT } m1_wake_result_t;
typedef struct {
    uint16_t baseline[M1_KEY_COUNT],frame[M1_KEY_COUNT];
    uint8_t drift[M1_KEY_COUNT],acquired;
    bool enabled[M1_KEY_COUNT],triggered[M1_KEY_COUNT],have_sequence;
    uint32_t sequence;
    m1_wake_result_t result;
} m1_wake_t;
/* One owner; call for each new sleep episode. NULL enables every real key.
 * Sensor IDs are compact board IDs; holes/battery samples are not keys. */
void m1_wake_init(m1_wake_t *s,const bool enabled[M1_KEY_COUNT]);
/* Complete canonical frames (1..4096) from successive single-shot captures.
 * Duplicate sequences do not advance filters. A lost/invalid frame latches a
 * fault: the outer manager must exit sleep, never invent a press from it.
 * Once wake is latched, the full triggering frame stays frozen until init. */
m1_wake_result_t m1_wake_frame(m1_wake_t *s,const uint16_t frame[M1_KEY_COUNT],uint32_t sequence);
/* Non-consuming copy for the wake/restoration owner. Triggered means movement,
 * NOT configured keyboard actuation or a velocity sample. No HID/MIDI events
 * are synthesized here. Invalid/unready requests leave outputs untouched. */
bool m1_wake_snapshot(const m1_wake_t *s,uint16_t frame[M1_KEY_COUNT],bool triggered[M1_KEY_COUNT]);
#endif
