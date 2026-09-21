#ifndef MIDI_TYPIST_CONTROL_H
#define MIDI_TYPIST_CONTROL_H
#include "midi_sysex.h"
/* Callbacks live for the service lifetime. write_events must copy accepted
 * bytes before returning. lock/unlock preserve the prior interrupt state. */
typedef struct {
    uint32_t (*millis)(void);
    bool (*ready)(void);
    bool (*write_events)(const uint8_t *,uint32_t);
    uint32_t (*lock)(void);
    void (*unlock)(uint32_t);
} midi_control_port_t;
bool midi_control_init(const midi_control_port_t *port);
void midi_control_command_handler(bool (*handler)(const char *));
void midi_control_usb_reset(void);
void midi_control_receive_usb(const uint8_t *events,size_t length);
void midi_control_service(void);
bool midi_control_ready(void);
bool midi_control_publish(uint8_t kind,const uint8_t *data,size_t length);
#endif
