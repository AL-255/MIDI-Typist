#ifndef MIDI_TYPIST_M1_SAVE_H
#define MIDI_TYPIST_M1_SAVE_H
#include "m1_live.h"
/* Pass this singleton port to m1_live_init to use actual hardware save gating.
 * The live owner must have already established fresh neutral input/output,
 * quiet settings and no menu/transport transition. One serialized foreground
 * owns all scanner/LED/radio offers. No IRQ may start their foreground work.
 * TMR2 must be running; the battery and lighting HALs must be initialized.
 * No flash is written here: READY holds PRIMASK until end and pauses only the
 * scanner. Links, output buffers, rails and LED/radio configuration remain
 * intact. End samples real elapsed time and resumes fresh periodic scanning.
 * DEFER changes no hardware. A fault latches until MCU reset, never retries.
 * Physical supply/timing validation and complete startup remain required. */
const m1_live_storage_ops_t *m1_save_ops(void);
bool m1_save_fault(void);
#endif
