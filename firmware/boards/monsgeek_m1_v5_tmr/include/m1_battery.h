#ifndef MIDI_TYPIST_M1_BATTERY_H
#define MIDI_TYPIST_M1_BATTERY_H
#include "m1_board.h"
#include "defaults.h"

/* Reference DMA destination 0x2000905a + 5*42 + 4*2 = 0x20009134.
 * This is an unused key position, ADC channel 0. Never key-normalize it. */
#define M1_BATTERY_BANK 5u
#define M1_BATTERY_RANK 4u
/* ADC-to-percent characterization from the board, not a battery voltage API. */
#define M1_BATTERY_ADC_EMPTY 1145u
#define M1_BATTERY_ADC_KNEE 1280u
#define M1_BATTERY_ADC_FULL 1705u

typedef enum {
    M1_CHARGER_UNKNOWN, M1_CHARGER_ON_BATTERY,
    M1_CHARGER_PIN_LOW, M1_CHARGER_PIN_HIGH
} m1_charger_status_t;
typedef struct {
    uint16_t samples[M1_BATTERY_FILTER_SAMPLES], average;
    uint32_t sampled_at;
    uint8_t count, percent, confirmations, charger_count;
    m1_charger_status_t charger, charger_candidate;
    bool valid, sample_clock, externally_powered, source_known;
} m1_battery_t;

void m1_battery_init(m1_battery_t *s);
uint8_t m1_battery_percent(uint16_t adc);
/* Call with a fresh COMPLETE raw scan and sampled pins. PC13 low means
 * external power; PB10's electrical charging/full polarity is not verified.
 * Its stable raw state is exposed honestly instead of inventing a label.
 * Rate limiting never reuses an old scan to manufacture filter samples. */
bool m1_battery_sample(m1_battery_t *s, uint16_t adc, bool pc13_high,
                       bool pb10_high, uint32_t now_ms);
void m1_battery_invalidate(m1_battery_t *s);
bool m1_battery_low(const m1_battery_t *s);
bool m1_battery_critical(const m1_battery_t *s);
/* Battery display owns a complete RGB frame while held. Low-battery overlay
 * suppresses ordinary lighting, as the reference does, but not key scanning. */
void m1_battery_lights(const m1_battery_t *s, bool requested,
                       uint8_t frame[M1_LED_BYTES], uint32_t now_ms);
#endif
