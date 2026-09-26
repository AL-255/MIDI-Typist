#ifndef MIDI_TYPIST_M1_TIME_H
#define MIDI_TYPIST_M1_TIME_H
#include <stdbool.h>
#include <stdint.h>

/* TMR2 in 32-bit plus mode, internal 216 MHz timer clock / 216. No pins,
 * DMA, interrupts, SysTick or DWT are owned by this foreground clock. */
#define M1_TIME_HZ 1000000u
#define M1_TIME_PLUS_BIT (1u<<10)
typedef struct { uint32_t us, ms; } m1_time_point_t;
/* Exclusive privileged foreground owner, preserving PRIMASK. Start once with
 * the normal board clocks and an unowned, stopped TMR2; timestamps start at 0.
 * Does not set clocks/rails or install interrupts. Repeated start is rejected. */
bool m1_time_start(void);
/* Sample at least once per 2^32 microseconds (about 71 minutes), including
 * masked-IRQ flash operations. ms wraps independently of us, not us/1000.
 * Clock/configuration loss latches a fault; output is unchanged on failure. */
bool m1_time_now(m1_time_point_t *out);
/* Stop/count the final tick before changing system clocks or deep sleep.
 * Resume only after restoring board clocks, with MEASURED elapsed microseconds
 * since suspend (not the requested sleep duration: wake can be early).
 * Retains fractional milliseconds. Invalid context/arguments do not mutate
 * state. An ownership/clock fault requires explicit stop/start, losing epoch.
 * Do NOT suspend for flash: leave TMR2 running to count masked-IRQ time. */
bool m1_time_suspend(m1_time_point_t *out);
bool m1_time_resume(uint32_t elapsed_us);
bool m1_time_stop(void);
bool m1_time_healthy(void);
#endif
