#ifndef MIDI_TYPIST_KEYBOARD_TELEMETRY_H
#define MIDI_TYPIST_KEYBOARD_TELEMETRY_H
#include <stddef.h>
#include "keyboard_app.h"

/* MTG3: board-independent, explicit-sized header + compact sensor records.
 * Addresses, hardware states and storage schemas never appear on this wire.
 * All multibyte fields are little endian; see docs/TELEMETRY.md. */
#define MT_GUI_HEADER_SIZE 80u
#define MT_GUI_RECORD_SIZE 17u
#define MT_GUI_MAX_KEYS 128u
#define MT_GUI_MAX_HID 32u
#define MT_GUI_SIZE(count,hid) (((MT_GUI_HEADER_SIZE + (count)*MT_GUI_RECORD_SIZE + (hid)+3u)&~3u) + 4u)
#define MT_GUI_MAX_SIZE MT_GUI_SIZE(MT_GUI_MAX_KEYS,MT_GUI_MAX_HID)
typedef struct {
    uint32_t now,sequence,ack,scan_errors,light_errors;
    uint32_t calibration_generation,storage_error,storage_generation;
    uint8_t result,storage_flags,storage_slot;
    bool scan_fault,light_fault,calibration_saved,calibration_supported;
} keyboard_telemetry_status_t;
size_t keyboard_telemetry_encode(const keyboard_app_t *app,
    const keyboard_telemetry_status_t *status,uint8_t *out,size_t capacity);
#endif
