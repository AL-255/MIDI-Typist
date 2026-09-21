#ifndef MIDI_TYPIST_M1_LIVE_H
#define MIDI_TYPIST_M1_LIVE_H
#include "m1_board.h"
#include "m1_controls.h"
#include "m1_factory.h"
/* Serialized foreground application owner. HAL clock/rails, periodic
 * scanner, lighting, battery inputs and USB lifecycle are initialized by the
 * outer startup/power coordinator, not implicitly retried here.
 * Init imports validated factory travel bounds read-only. Missing/invalid
 * calibration is rejected, never replaced with ADC rails. No flash writer yet:
 * calibration/RESET are unavailable and edits are explicitly volatile.
 * The outer owner initializes the chosen USB/radio transport. Wireless init
 * requires a healthy scheduler configured for that exact mode; link readiness
 * may follow later. Optional transport ops must outlive this owner and prove
 * host release/physical selection (local radio idle is not that proof).
 * NULL ops disable Fn transport selection, not wireless keyboard/battery use.
 * USB SysEx configuration stays available while typing over radio. Performance
 * MIDI is USB-only; HID goes to the selected transport. No pairing/power sequencing
 * or automatic link restart happens here. */
bool m1_live_init(m1_transport_t current,const m1_transport_ops_t *transports);
m1_factory_result_t m1_live_factory_result(void);
/* Both clocks wrap independently. Do not derive now_ms from wrapping now_us. */
void m1_live_service(uint32_t now_ms,uint32_t now_us);
/* Neutralizes application ownership and stops streams, not hardware/rails.
 * Continue service to drain neutral HID/MIDI cleanup before explicit init can
 * resume. Wireless restart also requires the existing transport ops to confirm
 * old-host releases. Reinitialization resets volatile settings; USB epochs do not. */
void m1_live_stop(uint32_t now_ms);
uint32_t m1_live_scan_losses(void);
m1_transport_t m1_live_transport(void);
/* A timed-out physical selection leaves host ownership ambiguous. This is
 * terminal for this owner; explicit init cannot silently reset the fault. */
bool m1_live_transport_fault(void);
#endif
