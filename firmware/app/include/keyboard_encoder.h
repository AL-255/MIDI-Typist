#ifndef MIDI_TYPIST_KEYBOARD_ENCODER_H
#define MIDI_TYPIST_KEYBOARD_ENCODER_H
#include <stdbool.h>
#include <stdint.h>
#include "defaults.h"

/* Logical events, deliberately not HID usages or analog sensor indices.
 * Positive is the electrical cycle 0,1,3,2,0, not a claim about orientation. */
enum { ENCODER_POSITIVE=1, ENCODER_NEGATIVE=2,
       ENCODER_PRESS=4, ENCODER_RELEASE=8 };
typedef struct {
    uint8_t phase,candidate,phase_count;
    int8_t movement;
    uint16_t button_count,button_samples;
    bool button,button_armed;
    uint32_t invalid_transitions;
} keyboard_encoder_t;

/* Caller supplies regularly sampled two-bit phases and logical pressed state.
 * Init/neutralization discards partial turns and suppresses a button held at
 * startup until it has been released. No allocation, USB or GPIO dependency. */
bool keyboard_encoder_init(keyboard_encoder_t *s,uint8_t phase,bool pressed,uint32_t hz);
uint8_t keyboard_encoder_sample(keyboard_encoder_t *s,uint8_t phase,bool pressed);
#endif
