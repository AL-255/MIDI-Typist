#ifndef MIDI_TYPIST_CONTROL_H
#define MIDI_TYPIST_CONTROL_H
#include "midi_sysex.h"
#include "keyboard_telemetry.h"
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
/* Every platform binds its shared app after midi_control_init. The service
 * implements calibration read uniformly; no peripheral/flash callback needed. */
void midi_control_bind_application(const keyboard_app_t *app);
/* Optional foreground-only readback. Must not poll peripherals or mutate
 * settings; omitted providers reject power status instead of inventing data. */
void midi_control_power_handler(bool (*handler)(keyboard_power_status_t *));
void midi_control_usb_reset(void);
void midi_control_receive_usb(const uint8_t *events,size_t length);
void midi_control_service(void);
bool midi_control_ready(void);
/* Foreground hint only, not a reservation. Avoid preparing/copying a bulk
 * payload while a reply or immutable transmission owns the slot. Publish
 * still rechecks readiness, including a USB reset between these calls. */
bool midi_control_publish_ready(void);
bool midi_control_publish(uint8_t kind,const uint8_t *data,size_t length);
#endif
