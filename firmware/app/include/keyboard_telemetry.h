#ifndef MIDI_TYPIST_KEYBOARD_TELEMETRY_H
#define MIDI_TYPIST_KEYBOARD_TELEMETRY_H
#include "keyboard_app.h"
#include "device_store.h"

/* HKG wire limits are independent of the application's allocation budgets.
 * A different wire contract is required for >65 keys or a larger NKRO report. */
#define KEYBOARD_TELEMETRY_SIZE 1152u
#define KEYBOARD_TELEMETRY_KEYS 65u
#define KEYBOARD_TELEMETRY_REPORT_BYTES 16u
typedef struct {
    uint32_t sequence, ack, scan_errors, light_errors;
    uint8_t result;
    bool scan_fault, light_fault, calibration_supported;
} keyboard_telemetry_t;

/* Pure encoder: no transport, clocks, hardware headers or mutable globals.
 * Call from the application's owner task so the complete view is coherent.
 * The board owns rate limiting and increments sequence after a successful
 * encode. NULL store describes a board without persistent storage. Invalid
 * pointers/layouts are rejected without modifying out. See TELEMETRY.md. */
bool keyboard_telemetry_encode(uint8_t out[KEYBOARD_TELEMETRY_SIZE],
    const keyboard_app_t *app, const device_store_t *store,
    const keyboard_telemetry_t *status, uint32_t now);
#endif
