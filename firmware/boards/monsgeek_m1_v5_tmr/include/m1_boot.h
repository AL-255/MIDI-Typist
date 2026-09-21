#ifndef MIDI_TYPIST_M1_BOOT_H
#define MIDI_TYPIST_M1_BOOT_H
#include "m1_live.h"

/* Cold application handoff, not a reset handler or a bootloader. The reset
 * owner must establish normal clocks, start m1_time, install the HAL IRQ
 * routes and prove all previous peripheral/host ownership is gone first.
 * Never use this to change transports, reconnect a cable or wake from sleep.
 * The supplied transport callbacks (optional) must outlive the application. */
typedef enum {
    M1_BOOT_OFF, M1_BOOT_RAILS, M1_BOOT_LINKS, M1_BOOT_RADIO,
    M1_BOOT_APPLICATION, M1_BOOT_READY, M1_BOOT_FAILED, M1_BOOT_CLOCK_FATAL
} m1_boot_state_t;
typedef enum {
    M1_BOOT_OK, M1_BOOT_TIME, M1_BOOT_STARTUP, M1_BOOT_SOURCE,
    M1_BOOT_PAUSE, M1_BOOT_USB, M1_BOOT_RADIO_INIT, M1_BOOT_RADIO_LINK,
    M1_BOOT_RESUME, M1_BOOT_APP
} m1_boot_error_t;
bool m1_boot_begin(m1_transport_t transport,const m1_transport_ops_t *ops,
                   bool cold_quiescent);
/* Single privileged foreground owner, preserving PRIMASK. Poll until READY
 * or failure; IRQs must run between calls for battery warmup acquisition.
 * Reads the actual timebase itself, refreshing after blocking HAL operations.
 * On READY, hand ownership to m1_live_service and the runtime power owner;
 * no further service/health monitoring occurs here. USB may still enumerate,
 * and a radio peer may still be unlinked: READY is not host readiness.
 * Failure is terminal until reset, without automatic peripheral retries.
 * Ordinary failures stop owned links/scanner/rails before any typing starts.
 * CLOCK_FATAL performs no peripheral cleanup and retains masked interrupts. */
void m1_boot_service(void);
m1_boot_state_t m1_boot_state(void);
m1_boot_error_t m1_boot_error(void);
#endif
