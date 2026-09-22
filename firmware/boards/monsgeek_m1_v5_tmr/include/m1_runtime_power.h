#ifndef MIDI_TYPIST_M1_RUNTIME_POWER_H
#define MIDI_TYPIST_M1_RUNTIME_POWER_H
#include <stdbool.h>
#include <stdint.h>
/* Sole foreground owner after boot. Calls live service while awake/draining;
 * after park, owns scan/LED/radio and the RTC sleep/wake sequence. The caller
 * must refresh both clocks and the physical power-source input after every
 * call (sleep can advance time), and trap on clock/time failure. Awake cable
 * changes have a separate parked owner; changes during sleep fail closed.
 * No flash writes, cold reinitialization of application state or automatic
 * recovery from an ambiguous peripheral failure. */
typedef enum {
    M1_RUNTIME_AWAKE, M1_RUNTIME_DRAIN, M1_RUNTIME_BLANK, M1_RUNTIME_PEER,
    M1_RUNTIME_SETTLE, M1_RUNTIME_SLEEP, M1_RUNTIME_SCAN_STAMP,
    M1_RUNTIME_SCAN_SETTLE, M1_RUNTIME_CAPTURE, M1_RUNTIME_DEEPEN,
    M1_RUNTIME_RESTORE_GPIO, M1_RUNTIME_RESTORE_RAILS,
    M1_RUNTIME_RESTORE_STAMP, M1_RUNTIME_RESTORE_SETTLE,
    M1_RUNTIME_RESTORE_RADIO, M1_RUNTIME_RESTORE_LINK,
    M1_RUNTIME_FAILED, M1_RUNTIME_CLOCK_FATAL, M1_RUNTIME_TIME_FATAL,
    M1_RUNTIME_SOURCE
} m1_runtime_power_state_t;
void m1_runtime_power_service(uint32_t now_ms,uint32_t now_us,bool external);
m1_runtime_power_state_t m1_runtime_power_state(void);
/* Zero while healthy; low byte is runtime stage plus one. For source changes,
 * bits 8..15 carry the source controller's stage-plus-one detail. */
uint32_t m1_runtime_power_error(void);
#endif
