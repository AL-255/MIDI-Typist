#ifndef MIDI_TYPIST_KEYBOARD_LIGHTING_H
#define MIDI_TYPIST_KEYBOARD_LIGHTING_H
#include <stdbool.h>
#include <stdint.h>
#include "keyboard_limits.h"

#define LIGHTING_FRAME_SIZE MT_LIGHT_FRAME_BYTES
/* A board-owned byte framebuffer. Only the board's setter knows its encoding.
 * Zero bytes mean all LEDs off; every byte is a linear intensity component,
 * so clearing/scaling is portable. A board with a different wire format
 * encodes from this framebuffer when submitting its hardware transfer. */
void keyboard_light_set(uint8_t profile, unsigned sensor, uint8_t *frame,
                        uint8_t red, uint8_t green, uint8_t blue);
/* Reads back what set() wrote, so application code can keep an existing
 * intensity and change only the hue. Out-of-range sensors read as zero. */
void keyboard_light_get(uint8_t profile, unsigned sensor, const uint8_t *frame,
                        uint8_t *red, uint8_t *green, uint8_t *blue);
/* Board-owned physical x position in quarter-key units, 0..64. This is
 * independent of electrical scan order and LED-chain wiring. */
uint8_t keyboard_light_x(uint8_t profile, unsigned sensor);
uint8_t lighting_travel_pwm(uint16_t raw, uint16_t lower, uint16_t upper);
void lighting_travel_frame(uint8_t profile, const uint16_t *raw, const uint16_t *lower,
                          const uint16_t *upper, bool valid, uint8_t *frame);
void lighting_rainbow_frame(uint8_t profile, unsigned count, uint8_t *frame, uint32_t now);
#endif
