#ifndef MIDI_TYPIST_M1_ENCODER_H
#define MIDI_TYPIST_M1_ENCODER_H
#include "keyboard_encoder.h"

typedef struct {
    uint32_t samples,positive,negative,invalid,overflows;
    uint8_t phase,queued;
    bool pressed,active,fault;
} m1_encoder_status_t;
/* Scanner owns sampling cadence. Start only with TMR6 stopped, pins restored
 * as pull-up inputs and GPIOC clock running. Stop before clock/pin changes.
 * No pin/clock writes are performed here. Start discards all old events. */
void m1_encoder_start(void);
void m1_encoder_stop(void);
void m1_encoder_irq(void);
/* Foreground only; each byte is a set of ENCODER_* events in sample order.
 * Overflow discards the queue and latches fault, never wraps into stale events.
 * Explicit start after neutralization is required to clear that fault. */
bool m1_encoder_take(uint8_t *events);
bool m1_encoder_status(m1_encoder_status_t *status);
#endif
