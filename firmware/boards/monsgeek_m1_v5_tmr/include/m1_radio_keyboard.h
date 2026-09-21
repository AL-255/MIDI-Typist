#ifndef MIDI_TYPIST_M1_RADIO_KEYBOARD_H
#define MIDI_TYPIST_M1_RADIO_KEYBOARD_H
#include "keyboard.h"
#include "m1_radio.h"

/* Reviewed peer report format, not the custom USB NKRO descriptor. */
#define M1_RADIO_KEY_SLOTS 6u
#define M1_RADIO_KEY_BITMAP_BYTES 15u
#define M1_RADIO_KEY_BITMAP_MAX (M1_RADIO_KEY_BITMAP_BYTES*8u-1u)
#define M1_RADIO_KEY_ROLLOVER 1u
enum { M1_RADIO_KEY_LIST=1, M1_RADIO_KEY_BITMAP=2 };
typedef struct {
    uint8_t slots[M1_RADIO_KEY_SLOTS];
    uint8_t bitmap[M1_RADIO_KEY_BITMAP_BYTES];
    uint8_t modifiers;
    bool rollover;
} m1_radio_keyboard_t;

/* Preserve the list/bitmap ownership of still-held usages. Modifiers are
 * separate; input is the already-remapped, duplicate-unioned common report.
 * If an unrepresentable usage overflows six slots, emit HID ErrorRollOver,
 * never index beyond the peer bitmap. Zero-initialize for a neutral baseline.
 * Calculate into a copy while packets are in flight, then commit the copy
 * only after BOTH subtypes finish local transmission. */
bool m1_radio_keyboard_update(m1_radio_keyboard_t *state,const keyboard_report_t *report);
/* Subtype 1 = modifiers + six slots; subtype 2 = 15-byte usage-indexed bitmap.
 * Always send both for a baseline/recovery; no reserved USB byte on the wire. */
bool m1_radio_keyboard_packet(const m1_radio_keyboard_t *state,unsigned subtype,
                             m1_radio_packet_t *packet);
#endif
