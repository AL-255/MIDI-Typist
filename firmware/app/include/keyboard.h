#ifndef MIDI_TYPIST_KEYBOARD_H
#define MIDI_TYPIST_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEYBOARD_NKRO_USAGE_MIN 0x04u
#ifndef MT_HID_USAGE_MAX
#define MT_HID_USAGE_MAX 0xdfu
#endif
#define KEYBOARD_NKRO_USAGE_MAX MT_HID_USAGE_MAX
#define KEYBOARD_NKRO_BITMAP_BYTES ((KEYBOARD_NKRO_USAGE_MAX-KEYBOARD_NKRO_USAGE_MIN+8u)/8u)
#define KEYBOARD_NKRO_REPORT_BYTES (2u+KEYBOARD_NKRO_BITMAP_BYTES)
_Static_assert(MT_HID_USAGE_MAX>=0x73u && MT_HID_USAGE_MAX<0xe0u,"invalid NKRO usage budget");

typedef struct
{
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[KEYBOARD_NKRO_BITMAP_BYTES];
} keyboard_report_t;

void keyboard_report_clear(keyboard_report_t *report);
bool keyboard_report_set_usage(keyboard_report_t *report, uint8_t usage, bool pressed);
bool keyboard_report_get_usage(const keyboard_report_t *report, uint8_t usage);
static inline bool keyboard_keycode_valid(unsigned usage)
{ return !usage || (usage>=KEYBOARD_NKRO_USAGE_MIN && usage<=KEYBOARD_NKRO_USAGE_MAX) ||
         (usage>=0xe0u && usage<=0xe7u); }

#endif
