#ifndef MIDI_TYPIST_M1_LIVE_H
#define MIDI_TYPIST_M1_LIVE_H
#include "m1_board.h"
/* Serialized foreground USB application owner. HAL clock/rails, periodic
 * scanner, lighting, battery inputs and USB lifecycle are initialized by the
 * outer startup/power coordinator, not implicitly retried here.
 * Bounds must be supplied in canonical units from a verified source; ADC
 * rails are not substituted for unknown key travel. No flash backend yet:
 * calibration/RESET are unavailable and edits are explicitly volatile.
 * Radio output/transport selection must still be integrated before this can
 * serve as the complete M1 application. */
bool m1_live_init(const uint16_t lower[M1_KEY_COUNT],const uint16_t upper[M1_KEY_COUNT]);
/* Both clocks wrap independently. Do not derive now_ms from wrapping now_us. */
void m1_live_service(uint32_t now_ms,uint32_t now_us);
/* Neutralizes application ownership and stops streams, not hardware/rails.
 * Continue service to drain neutral HID/MIDI cleanup before explicit init can
 * resume. Reinitialization resets volatile settings; USB epochs do not. */
void m1_live_stop(uint32_t now_ms);
uint32_t m1_live_scan_losses(void);
#endif
