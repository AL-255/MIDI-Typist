#ifndef MIDI_TYPIST_M1_CONTROLS_H
#define MIDI_TYPIST_M1_CONTROLS_H
#include "keyboard_app.h"
#include "m1_battery.h"

/* Values are the original radio mode byte (opcode 0x93), not USB IDs.
 * Modes 3/4 are intentionally not offered as Bluetooth slots. */
typedef enum {
    M1_TRANSPORT_BT1=0, M1_TRANSPORT_BT2=1, M1_TRANSPORT_BT3=2,
    M1_TRANSPORT_RADIO=5, M1_TRANSPORT_USB=6
} m1_transport_t;
typedef struct {
    /* True after the old transport's neutral-output handoff is complete.
     * The SPI protocol has no host-delivery ACK; document that boundary.
     * select returns true when the radio/USB mode change is confirmed.
     * Neither callback may block or mutate shared application state. */
    bool (*drained)(void *context);
    bool (*select)(void *context,m1_transport_t target);
    void *context;
    bool (*available)(void *context,m1_transport_t target); /* NULL: all offered */
    /* Optional: select target, issue one pairing request, then confirm fresh
     * mode status. True is NOT proof of a bond or connected host. Same-slot
     * requests are valid. Called repeatedly after drained until complete. */
    bool (*pair)(void *context,m1_transport_t target); /* NULL: no long-hold pairing */
} m1_transport_ops_t;
typedef struct {
    m1_transport_t current,target;
    const m1_transport_ops_t *ops;
    const m1_battery_t *battery;
    keyboard_text_t text;
    uint32_t revision, requested_at, errors, held_at;
    uint8_t held, pending;
    bool switching,battery_show,neutral_required,pairing;
} m1_controls_t;
bool m1_transport_valid(unsigned transport);
/* Binds only this board's optional application hooks; does not touch hardware.
 * Call after keyboard_app_init, before delivering the first scan. */
bool m1_controls_bind(m1_controls_t *s,keyboard_app_t *app,m1_transport_t current,
                       const m1_transport_ops_t *ops,const m1_battery_t *battery);
/* Call after keyboard_app_service has offered releases on the OLD transport.
 * A failed/absent physical adapter never masquerades as a successful switch. */
void m1_controls_service(m1_controls_t *s,keyboard_app_t *app,uint32_t now);
#endif
