#ifndef MIDI_TYPIST_M1_FACTORY_H
#define MIDI_TYPIST_M1_FACTORY_H
#include "m1_board.h"

/* Read-only ID2949 calibration records, not custom profile allocations.
 * The reference loader at 0x0800F814 imports 126 rank-major halfwords. */
#define M1_FACTORY_UPPER_ADDRESS 0x08032000u
#define M1_FACTORY_LOWER_ADDRESS 0x08032800u
#define M1_FACTORY_PAGE_BYTES 2048u
#define M1_FACTORY_CELL_COUNT (M1_NATIVE_ROW_CELLS * M1_BANK_COUNT)
#define M1_FACTORY_VALUES_BYTES (M1_FACTORY_CELL_COUNT * 2u)
#define M1_FACTORY_TRAILER_OFFSET (M1_FACTORY_PAGE_BYTES - 3u)
typedef struct {
    uint8_t values[M1_FACTORY_VALUES_BYTES];
    uint8_t trailer[3]; /* persisted flag, 0x55, 0xaa; padding is not imported */
} m1_factory_record_t;
typedef struct {
    uint16_t lower[M1_KEY_COUNT],upper[M1_KEY_COUNT];
} m1_factory_bounds_t;
typedef enum {
    M1_FACTORY_OK, M1_FACTORY_ARGUMENT, M1_FACTORY_CONTEXT, M1_FACTORY_BUSY,
    M1_FACTORY_MARKER, M1_FACTORY_UNCALIBRATED, M1_FACTORY_RANGE,
    M1_FACTORY_NOT_LOADED
} m1_factory_result_t;
/* All-or-nothing, no source mutation. Unused cells are not key bounds.
 * Accepts saved flag 1 only; never fabricates floors for absent calibration.
 * Valid native bounds are converted exactly like ADC samples (+1). */
m1_factory_result_t m1_factory_decode(const m1_factory_record_t *upper,
    const m1_factory_record_t *lower,m1_factory_bounds_t *out);
/* Native memory-mapped read only, serialized privileged foreground owner.
 * Rejects busy flash and preserves IRQ mask. No unlock/erase/program, no
 * key-type initialization, no arbitrary-address reader. Caller must prevent
 * concurrent flash writes. Failed validation leaves output unchanged. */
m1_factory_result_t m1_factory_load(m1_factory_bounds_t *out);
#endif
