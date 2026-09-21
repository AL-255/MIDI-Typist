#ifndef MIDI_TYPIST_M1_POWER_H
#define MIDI_TYPIST_M1_POWER_H
#include "m1_controls.h"

/* Radio selector values consumed by original 0x080178d8, distinct from mode.
 * Do not infer an RF connection solely from a USB enumeration. */
typedef struct {
    m1_transport_t transport;
    uint8_t selector;
    bool externally_powered, blocked, fast_idle, activity;
    const m1_battery_t *battery;
} m1_power_input_t;
typedef struct {
    uint32_t elapsed;
    uint16_t bluetooth_idle, radio_idle;
    uint8_t periodic, radio_command;
    bool critical_latched, sleep_requested, radio_committed;
} m1_power_t;
/* Idle limits are qualified periodic steps, not scan frames or milliseconds.
 * Zero disables connected-host idle sleep only; critical protection remains. */
void m1_power_init(m1_power_t *s,uint16_t bluetooth_idle,uint16_t radio_idle);
/* Call once per original-style periodic service slot, NOT per 8 kHz frame.
 * This stages radio command 3/5. It does not put hardware to sleep. */
void m1_power_tick(m1_power_t *s,const m1_power_input_t *in);
void m1_power_activity(m1_power_t *s);
/* A completed radio transaction is mandatory before clocks/rails go away.
 * Then the platform must finish releasing reports, blank LEDs, pause ADC and
 * reduce USB PHY power. Pending/busy flags are not completion evidence. */
bool m1_power_radio_committed(m1_power_t *s,uint8_t command);
/* Local prerequisites only: the outer coordinator must also establish the
 * transport-specific sleep handoff before quiescing peer GPIO or power rails.
 * Critical escalation to command 3 invalidates a previous command-5 commit. */
bool m1_power_can_sleep(const m1_power_t *s,bool reports_drained,bool scan_stopped,
                        bool leds_off,bool radio_idle,bool usb_quiescent);
/* Invoke only after clocks/rails/scan/radio have actually been restored. */
void m1_power_woke(m1_power_t *s);
#endif
