#ifndef MIDI_TYPIST_M1_STARTUP_H
#define MIDI_TYPIST_M1_STARTUP_H
#include <stdbool.h>
#include <stdint.h>

/* Early startup only: interrupts masked, SysTick and peripherals quiescent.
 * These are callable HAL components, not a reset handler or boot image.
 * TMR2 timebase must be suspended before clock changes.
 * No VTOR, USB, bootloader, flash program/erase or factory-data operations. */
typedef enum {
    M1_CLOCK_OK, M1_CLOCK_INTERRUPTS, M1_CLOCK_HICK, M1_CLOCK_HICK_SWITCH,
    M1_CLOCK_PLL_STOP, M1_CLOCK_HEXT, M1_CLOCK_PLL, M1_CLOCK_PLL_SWITCH,
    M1_CLOCK_RATE, M1_CLOCK_HEXT_STOP, M1_CLOCK_TIMEBASE
} m1_clock_result_t;
m1_clock_result_t m1_clock_init(void);

/* Single privileged foreground owner. Caller proves existing transports and
 * peripherals quiescent. PC13 selects wired or battery startup and must remain
 * in that state while this owner is serviced. No radio mode is selected here. Begin once, then
 * service with independently wrapping clocks; service may enter RTC sleep on
 * the battery path. Start m1_time beforehand: battery startup measures the
 * RTC against it, then bridges actual sleep time without losing the epoch.
 * Refresh both time readings after it returns from sleep.
 * A nonfatal failure stops owned HALs/rails; explicitly stop before retrying.
 * If GPIO restoration fails, prepared ownership still blocks a new begin. */
bool m1_startup_begin(uint32_t now_ms,bool platform_quiescent);
void m1_startup_service(uint32_t now_ms,uint32_t now_us);
void m1_startup_stop(void);
bool m1_startup_ready(void);
bool m1_startup_fault(void);
/* Fatal clock restoration retains masked interrupts and stopped SysTick.
 * Stop/service do no further peripheral work; do not resume the application. */
bool m1_startup_clock_fatal(void);
bool m1_startup_encoder_phase(uint8_t *phase);
#endif
