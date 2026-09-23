#ifndef MIDI_TYPIST_KEYBOARD_TELEMETRY_H
#define MIDI_TYPIST_KEYBOARD_TELEMETRY_H
#include <stddef.h>
#include "keyboard_app.h"

/* MTG4: board-independent, explicit-sized header + compact sensor records.
 * Addresses, hardware states and storage schemas never appear on this wire.
 * All multibyte fields are little endian; see docs/TELEMETRY.md. */
#define MT_GUI_HEADER_SIZE 80u
#define MT_GUI_RECORD_SIZE 17u
#define MT_GUI_MAX_KEYS 128u
#define MT_GUI_MAX_HID 32u
#define MT_GUI_SIZE(count,hid) (((MT_GUI_HEADER_SIZE + (count)*MT_GUI_RECORD_SIZE + (hid)+3u)&~3u) + 4u)
#define MT_GUI_MAX_SIZE MT_GUI_SIZE(MT_GUI_MAX_KEYS,MT_GUI_MAX_HID)
/* MTB1, shared calibration/input readback. Records are sample/lower/upper/
 * control uint16 values. This reads active bounds, never staged candidates or
 * arbitrary flash. The accompanying MTG4 storage status proves persistence. */
#define MT_BOUNDS_HEADER_SIZE 20u
#define MT_BOUNDS_RECORD_SIZE 8u
#define MT_BOUNDS_SIZE(count) (MT_BOUNDS_HEADER_SIZE+(count)*MT_BOUNDS_RECORD_SIZE)
enum { MT_BOUNDS_VALID=1u,MT_BOUNDS_NORMALIZED=2u };
size_t keyboard_bounds_encode(const keyboard_app_t *app,uint32_t now,
                              uint8_t *out,size_t capacity);
enum { MT_TRANSPORT_UNKNOWN,MT_TRANSPORT_USB,MT_TRANSPORT_BT1,
       MT_TRANSPORT_BT2,MT_TRANSPORT_BT3,MT_TRANSPORT_RADIO };
enum { MT_TRANSPORT_READY=1u,MT_TRANSPORT_SWITCHING=2u,MT_TRANSPORT_PAIRING=4u,
       /* Build has no Bluetooth/2.4 GHz feature: no wireless transport can be
        * selected and no pairing request exists. Hosts must not advertise them. */
       MT_TRANSPORT_USB_ONLY=8u };
/* Optional read-only power status, returned in the command ACK, not the
 * latest-only key stream. Unverified charger polarity has explicit raw states. */
#define MT_POWER_SIZE 16u
enum { MT_POWER_SOURCE_KNOWN=1u,MT_POWER_EXTERNAL=2u,MT_POWER_VALID=4u,
       MT_POWER_LOW=8u,MT_POWER_CRITICAL=16u };
enum { MT_CHARGE_UNKNOWN,MT_CHARGE_BATTERY,MT_CHARGE_ACTIVE,MT_CHARGE_FULL,
       MT_CHARGE_RAW_LOW,MT_CHARGE_RAW_HIGH };
typedef struct {
    uint8_t flags,percent,charger;
    uint16_t adc; /* UINT16_MAX when unavailable; board-native, not key travel. */
    uint32_t age_ms; /* Latest filter input age; UINT32_MAX when unavailable. */
} keyboard_power_status_t;
size_t keyboard_power_encode(const keyboard_power_status_t *status,
                             uint8_t *out,size_t capacity);
typedef struct {
    uint32_t now,sequence,ack,scan_errors,light_errors;
    uint32_t calibration_generation,storage_error,storage_generation;
    uint8_t result,storage_flags,storage_slot;
    uint8_t transport,transport_flags;
    bool scan_fault,light_fault,calibration_saved,calibration_supported;
} keyboard_telemetry_status_t;
size_t keyboard_telemetry_encode(const keyboard_app_t *app,
    const keyboard_telemetry_status_t *status,uint8_t *out,size_t capacity);
#endif
