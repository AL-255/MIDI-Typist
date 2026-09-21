#ifndef MIDI_TYPIST_M1_DIAGNOSTICS_H
#define MIDI_TYPIST_M1_DIAGNOSTICS_H
#include <stdbool.h>
#include <stdint.h>
/* Foreground cold-start owner only. Uses an already running USB instance; no
 * peripheral restart or flash write. Stops owning SysEx once boot is READY.
 * Returns true only for an active-session bootloader request with an
 * armed recovery flag. The caller, not this service, performs the reset. */
bool m1_diagnostics_service(uint32_t now_ms);
#endif
