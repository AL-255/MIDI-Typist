#ifndef MIDI_TYPIST_M1_HAL_H
#define MIDI_TYPIST_M1_HAL_H
#include "m1_board.h"
/* Board startup must establish the verified 216 MHz clock and sensor power.
 * Init configures owned ADC/mux pins only, leaves acquisition stopped, and
 * fails on incompatible clocks or bounded ADC calibration timeout.
 * No bootloader, flash, radio, LED-power or factory-record writes occur here.
 */
bool m1_hal_init(void);
bool m1_hal_start(void);
/* One nonperiodic, complete six-bank frame for sleep/wake checks. Init and
 * sensor/clock settling remain caller responsibilities. Rejects busy or
 * unconsumed frames. Completion stops ADC/timers/DMA but keeps configuration
 * healthy, allowing the next capture without recalibration. Consume via frame.
 * These frames are NOT the normal 8 kHz stream and must not feed velocity. */
bool m1_hal_capture_start(uint32_t now_us);
bool m1_hal_capture_busy(void);
/* Foreground timeout service is mandatory while a one-shot is outstanding. */
void m1_hal_service(uint32_t now_us);
/* Stop/fault invalidates acquisition. Initialize again before restarting. */
void m1_hal_stop(void);
/* Bind these from the application vectors, never from shared code. */
void m1_hal_timer_irq(void);
void m1_hal_dma_irq(void);
bool m1_hal_frame(uint16_t frame[M1_KEY_COUNT],uint32_t *sequence);
/* Same complete-frame sequence as keys, native ADC counts, non-consuming.
 * Caller must reject a sequence it has already sampled. */
bool m1_hal_battery(uint16_t *adc,uint32_t *sequence);
bool m1_hal_healthy(void);
/* One-shot wake captures must never enter the periodic velocity pipeline. */
bool m1_hal_periodic_active(void);
uint32_t m1_hal_errors(void);
#endif
