#ifndef MIDI_TYPIST_FUN60_BOARD_H
#define MIDI_TYPIST_FUN60_BOARD_H
#include <stdbool.h>
#include <stdint.h>

#define FUN60_KEY_COUNT 61u
#define FUN60_ADC_ROWS 14u
#define FUN60_MUX_CHANNELS 6u /* channel zero is not a key */
#define FUN60_PROFILE 4u
enum { FUN60_KEY_ESCAPE=1, FUN60_KEY_TAB=15, FUN60_KEY_CAPS=29, FUN60_KEY_FN=59 };
#define FUN60_CORE_HZ 216000000u
#define FUN60_XTAL_HZ 12000000u
#define FUN60_LED_BYTES (FUN60_KEY_COUNT * 24u)
#define FUN60_ADC_FULL_SCALE 4095u
#define FUN60_APP_BASE 0x08005000u
#define FUN60_VECTOR_BASE 0x08005200u
#define FUN60_IAP_END 0x08028000u
/* Reserve these complete erase sectors INSIDE our application allocation.
 * Factory data starts at IAP_END and is never a custom storage target. */
#define FUN60_STORE_BASE 0x08027000u
#define FUN60_STORE_SECTOR_BYTES 2048u
#define FUN60_STORE_SLOTS 2u

typedef struct {
    const char *label;
    uint8_t usage, row, channel, led, x4, y, width4;
} fun60_key_t;
extern const fun60_key_t fun60_keys[FUN60_KEY_COUNT];
int fun60_sensor(unsigned row, unsigned channel);
/* RGB triples indexed by sensor -> the hardware's GRB SPI bit-cell frame. */
void fun60_encode_lights(const uint8_t rgb[FUN60_KEY_COUNT*3u],
                         uint8_t wire[FUN60_LED_BYTES]);

/* Complete electrical scan, no actuation/filtering. Callbacks expose logical
 * signals, not register addresses. Only publishes output after ALL rows pass.
 * Hardware implementation uses the official AT32 SDK; native tests trace IO.
 * The gate remains asserted and the mux parked on every exit, including error.
 */
typedef enum { FUN60_DATA, FUN60_CLOCK, FUN60_GATE } fun60_signal_t;
typedef struct {
    void (*signal)(fun60_signal_t signal, bool high);
    void (*mux)(unsigned channel);
    void (*delay_us)(unsigned us);
    bool (*adc)(uint16_t *value);
} fun60_scan_io_t;
bool fun60_scan(const fun60_scan_io_t *io, uint16_t samples[FUN60_KEY_COUNT]);

/* AT32 board implementation. No flash writes or bootloader entry in HAL init. */
bool fun60_hardware_init(void);
bool fun60_hardware_scan(uint16_t samples[FUN60_KEY_COUNT]);
bool fun60_hardware_lights(const uint8_t rgb[FUN60_KEY_COUNT*3u]);
#endif
