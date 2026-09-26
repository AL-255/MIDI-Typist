#ifndef MIDI_TYPIST_M1_HAL_H
#define MIDI_TYPIST_M1_HAL_H
#include "m1_board.h"
/* Board startup must establish the verified 216 MHz clock and sensor power.
 * Init configures owned ADC/mux and digital encoder pins, leaves acquisition stopped, and
 * fails on incompatible clocks or bounded ADC calibration timeout.
 * No bootloader, flash, radio, LED-power or factory-record writes occur here.
 */
bool m1_hal_init(void);
bool m1_hal_start(void);
/* Foreground-only periodic pause/resume, preserving PRIMASK. Pause stops
 * ADC/timers/DMA and discards partial/unread frames, encoder events and battery data, retaining
 * ADC configuration/calibration and complete-frame sequence/errors. Not valid
 * for one-shot captures. A latched DMA/overrun fault fails instead of pausing.
 * Resume requires the original clocks/rails retained by the owner, starts at
 * bank zero after a full frame period, and does not recalibrate. Start/capture
 * cannot bypass a pause. Stop/fault requires init, not resume.
 * Owner MUST publish a scan gap and clear held-key/velocity state; a successful
 * pause alone is NOT proof of flash safety, power, or other DMA quiescence. */
bool m1_hal_pause(void);
bool m1_hal_resume(void);
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
/* Current queue depth in bits 0..15, peak since HAL init in bits 16..31. */
uint32_t m1_hal_queue_state(void);
enum { M1_SCAN_FAULT_NONE, M1_SCAN_FAULT_ADC_CALIBRATION, M1_SCAN_FAULT_PAUSE_OVERRUN,
       M1_SCAN_FAULT_DMA, M1_SCAN_FAULT_CAPTURE_TIMEOUT, M1_SCAN_FAULT_PERIOD_OVERRUN,
       M1_SCAN_FAULT_BANK };
uint32_t m1_hal_fault_reason(void); /* retained first fault, cleared by init */
/* Six packed 5-bit DMA remaining counts, bank 0 in low bits, sampled after
 * enabling DMA but before starting each row's trigger. Read-only diagnostics. */
uint32_t m1_hal_pretrigger_counts(void);
#endif
