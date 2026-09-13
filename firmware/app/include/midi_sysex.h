#ifndef MIDI_TYPIST_SYSEX_H
#define MIDI_TYPIST_SYSEX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Experimental/non-commercial SysEx namespace, not a registered product ID.
 * USB cable 0 is performance; cable 1 is this independent control port. */
#define MT_SYSEX_CABLE 1u
#define MT_SYSEX_VERSION 1u
#define MT_SYSEX_MAX_PAYLOAD 1152u
#define MT_SYSEX_WIRE_SIZE(n) (7u + (n) + 14u + (((n) + 20u) / 7u))
#define MT_SYSEX_MAX_WIRE MT_SYSEX_WIRE_SIZE(MT_SYSEX_MAX_PAYLOAD)
enum { MT_HELLO=1, MT_READY, MT_COMMAND, MT_ACK, MT_SNAPSHOT,
       MT_SAMPLES, MT_LOG, MT_ERROR, MT_DUMP, MT_KEEPALIVE, MT_CLOSE };
typedef struct { uint32_t session, sequence; uint16_t length; uint8_t kind; } midi_sysex_info_t;
size_t midi_sysex_encode(uint8_t kind, uint32_t session, uint32_t sequence,
                        const uint8_t *payload, size_t length, uint8_t *out, size_t capacity);
bool midi_sysex_decode(const uint8_t *wire, size_t length, midi_sysex_info_t *info,
                       uint8_t *payload, size_t capacity);
#endif
