#ifndef MIDI_TYPIST_M1_POWER_GPIO_H
#define MIDI_TYPIST_M1_POWER_GPIO_H
#include <stdbool.h>
#include <stdint.h>

/* Serialized privileged foreground owner, with restored main clocks and idle
 * scan/output peripherals. Permission includes host/radio release and power
 * sequencing; these helpers never infer it from DMA completion alone.
 * Rejects busy peripherals/live USB without writes; preserves PRIMASK. */
bool m1_power_gpio_prepare(bool platform_quiescent);
/* Normal startup or post-wake restore: PB12 output low; PB10/PA11 and the
 * encoder pins become pull-up inputs. Returns PC10 | (PC12 << 1), using the
 * reference's separate GPIO reads, for the encoder owner's neutral baseline.
 * A cable reconnect does not prevent restoring pins while USB is still idle. */
bool m1_power_gpio_restore(bool platform_quiescent,uint8_t *encoder_phase);
/* While prepared, PB10 is an OUTPUT, not a valid charge-status observation.
 * Battery acquisition and USB startup must defer until restore succeeds. */
bool m1_power_gpio_prepared(void);
/* Raw wake inputs: PC10, PC12, PC11 -> bits 0,1,2. Requires input configuration.
 * Sequential pin reads are not an atomic physical snapshot or a key event. */
bool m1_power_gpio_switches(uint8_t *switches);
#endif
