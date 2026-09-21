#ifndef MIDI_TYPIST_KEYBOARD_SAMPLE_H
#define MIDI_TYPIST_KEYBOARD_SAMPLE_H
#include <stdbool.h>
#include <stdint.h>
/* Map descending or ascending endpoints to canonical released=4096/pressed=1.
 * Board acquisition uses ADC electrical endpoints. The optional application
 * travel stage separately uses per-key calibration; never confuse the domains.
 * Handles 16-bit inputs. Clips out-of-range values.
 * Equal endpoints are an invalid board configuration, not a released key. */
bool keyboard_sample_normalize(uint16_t native, uint16_t released_full_scale,
                               uint16_t pressed_full_scale, uint16_t *canonical);
#endif
