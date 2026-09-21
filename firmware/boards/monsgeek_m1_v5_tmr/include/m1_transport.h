#ifndef MIDI_TYPIST_M1_TRANSPORT_H
#define MIDI_TYPIST_M1_TRANSPORT_H
#include "m1_controls.h"
/* Runtime Fn transport owner. No flash, pairing, rail/clock changes or USB
 * disconnect: a wired control link remains available in wireless mode.
 * Call service once per live iteration before offering transport callbacks. */
void m1_transport_service(uint32_t now_us);
const m1_transport_ops_t *m1_transport_ops(void);
#endif
