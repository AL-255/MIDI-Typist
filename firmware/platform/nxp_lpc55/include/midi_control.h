#ifndef MIDI_TYPIST_CONTROL_H
#define MIDI_TYPIST_CONTROL_H
#include "midi_sysex.h"
void midi_control_init(void);
void midi_control_command_handler(bool (*handler)(const char *));
void midi_control_usb_reset(void);
void midi_control_receive_usb(const uint8_t *events,size_t length);
void midi_control_service(void);
bool midi_control_ready(void);
bool midi_control_publish(uint8_t kind,const uint8_t *data,size_t length);
#endif
