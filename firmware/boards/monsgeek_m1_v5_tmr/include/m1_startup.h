#ifndef MIDI_TYPIST_M1_STARTUP_H
#define MIDI_TYPIST_M1_STARTUP_H
#include <stdbool.h>
#include <stdint.h>

/* Early startup only: interrupts masked, SysTick and peripherals quiescent.
 * These are callable HAL components, not a reset handler or boot image.
 * No VTOR, USB, bootloader, flash program/erase or factory-data operations. */
typedef enum {
    M1_CLOCK_OK, M1_CLOCK_INTERRUPTS, M1_CLOCK_HICK, M1_CLOCK_HICK_SWITCH,
    M1_CLOCK_PLL_STOP, M1_CLOCK_HEXT, M1_CLOCK_PLL, M1_CLOCK_PLL_SWITCH,
    M1_CLOCK_RATE, M1_CLOCK_HEXT_STOP
} m1_clock_result_t;
m1_clock_result_t m1_clock_init(void);

/* Single foreground owner. After successful clock init and a running
 * millisecond timebase, begin once and poll until ready. PC13 must stay low
 * (wired path). A failure stops HALs and deasserts the owned power controls.
 * Never call begin again to retry a live startup; explicitly stop first. */
bool m1_startup_begin(uint32_t now_ms);
void m1_startup_service(uint32_t now_ms);
void m1_startup_stop(void);
bool m1_startup_ready(void);
bool m1_startup_fault(void);
#endif
