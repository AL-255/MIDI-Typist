#ifndef MIDI_TYPIST_KEYBOARD_AUX_H
#define MIDI_TYPIST_KEYBOARD_AUX_H
#include "keyboard_encoder.h"
/* Serialized consumer pulses from digital input. Mapping order is electrical
 * positive, negative, button press. Button release does not repeat an action.
 * Transport callbacks copy the usage before returning true; false retries. */
typedef struct {
    uint16_t mapping[3],usage;
    uint32_t pressed_at;
    uint8_t pending;
    bool release,allowed;
} keyboard_aux_t;
void keyboard_aux_init(keyboard_aux_t *s,const uint16_t mapping[3]);
void keyboard_aux_cancel(keyboard_aux_t *s);
bool keyboard_aux_idle(const keyboard_aux_t *s);
bool keyboard_aux_offer(keyboard_aux_t *s,uint8_t events);
void keyboard_aux_service(keyboard_aux_t *s,bool allowed,uint32_t now,bool (*send)(uint16_t usage));
#endif
