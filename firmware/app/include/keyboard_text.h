#ifndef KEYBOARD_TEXT_H
#define KEYBOARD_TEXT_H
#include <stdbool.h>
#include <stdint.h>
#include "defaults.h"
typedef struct {
    uint32_t started_at;
    uint8_t sensors[KEYBOARD_TEXT_MAX];
    uint8_t length, profile, color[3];
} keyboard_text_t;
/* Copies up to 32 ASCII letters (case insensitive) and +/-/?; ignores others.
 * Empty/unsupported input stops the display. No borrowed string or allocation. */
void keyboard_text_start(keyboard_text_t *s, uint8_t profile, const char *text, uint32_t now);
void keyboard_text_stop(keyboard_text_t *s);
void keyboard_text_color(keyboard_text_t *s, uint8_t red, uint8_t green, uint8_t blue);
/* Replaces the frame while active: 30%/100% of the selected color (default white).
 * Nonblocking: 200 ms per character, 500 ms background-only repeat gap. */
bool keyboard_text_render(const keyboard_text_t *s, uint8_t *frame, uint32_t now);
#endif
