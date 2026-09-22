#ifndef MIDI_TYPIST_M1_DIAGNOSTICS_H
#define MIDI_TYPIST_M1_DIAGNOSTICS_H
#include <stdbool.h>
#include <stdint.h>
/* Foreground cold-start owner only. Uses an already running USB instance; no
 * peripheral restart or flash write. Stops owning SysEx once boot is READY.
 * Returns true only for an active-session bootloader request with an
 * qualified recovery page. The caller must quiesce peripherals, program/verify
 * the recovery flag and only then reset; this service never writes flash. */
bool m1_diagnostics_service(uint32_t now_ms);
/* Terminal valid-clock runtime failure. Caller has stopped scan/LED/radio
 * owners, but retains USB and rails. Rebinds control, releases wired reports,
 * and accepts read-only diagnostics/software IAP; never resumes application. */
void m1_diagnostics_runtime_fault(uint32_t detail);
#endif
