#ifndef MIDI_TYPIST_KEYBOARD_LIMITS_H
#define MIDI_TYPIST_KEYBOARD_LIMITS_H

/* Compile-time storage budgets selected by the board's CMake target.
 * No allocation occurs in the scan loop. 0 and 255 are reserved key IDs. */
#ifndef MT_KEY_CAPACITY
#define MT_KEY_CAPACITY 128u
#endif
#ifndef MT_LIGHT_FRAME_BYTES
#define MT_LIGHT_FRAME_BYTES (MT_KEY_CAPACITY * 3u)
#endif
#define MT_KEY_BITMAP_BYTES ((MT_KEY_CAPACITY + 7u) / 8u)
#define MT_KEY_BITMAP_WORDS ((MT_KEY_CAPACITY + 31u) / 32u)
_Static_assert(MT_KEY_CAPACITY > 0 && MT_KEY_CAPACITY < 255, "unsupported key capacity");
#endif
