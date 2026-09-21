#ifndef MIDI_TYPIST_M1_SLEEP_H
#define MIDI_TYPIST_M1_SLEEP_H
#include <stdbool.h>
#include <stdint.h>

/* LICK / (7+1) / (7+1), then the 16-bit CK_B wake counter. These are board
 * clock-tree settings, NOT measured milliseconds. Never reset backup storage. */
#define M1_RTC_DIV_A 7u
#define M1_RTC_DIV_B 7u
#define M1_RTC_MAX_TICKS 65536u
/* Reference 0x08016900 and RM 3.6/3.7.3: 1.0 V for low-power deep sleep.
 * The current SDK enum omits selector 0; use its field-setting macro. */
#define M1_SLEEP_LDO_SELECTOR 0u
typedef enum {
    M1_SLEEP_TIMER, M1_SLEEP_OTHER_WAKE, M1_SLEEP_NOT_READY,
    M1_SLEEP_CONTEXT, M1_SLEEP_BUSY, M1_SLEEP_RTC_ERROR,
    M1_SLEEP_HICK_ERROR, M1_SLEEP_CLOCK_FATAL
} m1_sleep_result_t;
/* Foreground initialization after m1_clock_init. Owns the RTC wake timer,
 * EXINT22 and IRQ3, not the calendar/date or backup registers. Rejects a
 * different existing backup clock instead of resetting that domain. */
bool m1_sleep_init(void);
bool m1_sleep_ready(void);
void m1_sleep_irq(void);
/* Requires privileged thread mode, BASEPRI=FAULTMASK=0. Caller proves radio,
 * USB PHY, key reports, power rails and all periodic IRQs quiescent, e.g. via
 * m1_power_can_sleep. False permission never writes hardware. Owned DMA and
 * scan timers are checked again. Other pending IRQs may cause an early wake.
 * PRIMASK is held through WFI and clock restoration (ARM DUI0553A 2.5.2).
 * TIMER means an RTC flag was observed, not a measured elapsed duration.
 * CLOCK_FATAL leaves interrupts masked and SysTick disabled: caller must not
 * resume ordinary application/peripheral operation. No reset is performed. */
m1_sleep_result_t m1_sleep_wait(uint32_t ticks,bool platform_quiescent);
#endif
