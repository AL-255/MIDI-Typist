#ifndef MIDI_TYPIST_M1_LIGHTING_H
#define MIDI_TYPIST_M1_LIGHTING_H
#include <stddef.h>
#include "m1_board.h"

/* Wiring/protocol facts: SPI2 TX, PA10 AF5, DMA1 channel 1/request 13.
 * RGB input is already in LED-chain order, as written by keyboard_light_set.
 * Each source bit is one MSB-first SPI byte; LEDs receive GRB components. */
#define M1_LED_SPI_DIV 16u
#define M1_LED_SYMBOL_ZERO 0xc0u
#define M1_LED_SYMBOL_ONE 0xf0u
#define M1_LED_WIRE_BYTES (M1_LED_BYTES * 8u)
bool m1_lighting_encode(const uint8_t *rgb,size_t length,uint8_t *wire,size_t capacity);

/* Serialized foreground owner only. Caller establishes board clock and LED
 * power first; this driver never toggles PB13 or other power/radio/scan pins.
 * now_us is a wrapping monotonic microsecond clock. Poll service regularly.
 * A successful offer copies the entire frame; busy offers do not change DMA
 * memory. Completion includes SPI drain and an explicit low latch interval.
 * A fault/stop requires reinitialization; no implicit retries or spin waits. */
bool m1_lighting_init(uint32_t now_us);
bool m1_lighting_offer(const uint8_t *rgb,size_t length,uint32_t now_us);
void m1_lighting_service(uint32_t now_us);
void m1_lighting_stop(void);
bool m1_lighting_ready(void);
bool m1_lighting_healthy(void);
uint32_t m1_lighting_errors(void);
#endif
