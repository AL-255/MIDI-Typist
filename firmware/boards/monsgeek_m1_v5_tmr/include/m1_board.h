#ifndef MIDI_TYPIST_M1_BOARD_H
#define MIDI_TYPIST_M1_BOARD_H
#include <stdbool.h>
#include <stdint.h>

#define M1_KEY_COUNT 82u
#define M1_PROFILE 1u
#define M1_BANK_COUNT 6u
#define M1_ADC_RANKS 15u
#define M1_NATIVE_ROW_CELLS 21u
#define M1_ADC_MAX 4095u
#define M1_FN_SENSOR 77u
#define M1_TAB_SENSOR 29u
#define M1_CAPS_SENSOR 44u
#define M1_ESC_SENSOR 0u
#define M1_DIGIT1_SENSOR 15u
#define M1_LEFT_ALT_SENSOR 74u
#define M1_SPACE_SENSOR 75u
#define M1_LED_BYTES (M1_KEY_COUNT * 3u)
#define M1_CORE_HZ 216000000u
#define M1_ADC_TRIGGER_HZ 3000000u

typedef struct { uint8_t bank, rank, usage; } m1_key_t;
extern const m1_key_t m1_keys[M1_KEY_COUNT];
/* MCU ADC channels in conversion order, distinct from logical ranks. */
extern const uint8_t m1_adc_channels[M1_ADC_RANKS];
/* PB9/PB8/PB7 output levels packed in bits 2/1/0. */
extern const uint8_t m1_bank_bits[M1_BANK_COUNT];
uint8_t m1_led_index(unsigned sensor);
/* Original per-cell storage is rank-major, unlike DMA's bank-major rows. */
unsigned m1_factory_cell(unsigned sensor);

/* Single owner, or callers must hold the scanner's IRQ critical section.
 * Only complete, ordered six-bank acquisitions are published. */
typedef struct {
    uint16_t rows[M1_BANK_COUNT][M1_ADC_RANKS];
    uint16_t latest[M1_KEY_COUNT];
    uint16_t battery;
    uint32_t sequence, errors, overwritten;
    uint8_t next_bank;
    bool pending, battery_valid;
} m1_scan_t;
void m1_scan_init(m1_scan_t *scan);
void m1_scan_fault(m1_scan_t *scan);
bool m1_scan_bank(m1_scan_t *scan, unsigned bank, const uint16_t *values);
bool m1_scan_take(m1_scan_t *scan, uint16_t *frame, uint32_t *sequence);
bool m1_scan_battery(const m1_scan_t *scan, uint16_t *adc, uint32_t *sequence);
#endif
