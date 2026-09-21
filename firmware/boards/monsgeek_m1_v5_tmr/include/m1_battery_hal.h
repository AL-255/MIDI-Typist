#ifndef MIDI_TYPIST_M1_BATTERY_HAL_H
#define MIDI_TYPIST_M1_BATTERY_HAL_H
#include "m1_battery.h"
/* Serialized foreground owner, independent of scan/lighting DMA channels.
 * Init only configures PB10/PC13 as pull-up inputs. No charger enable,
 * supply control, USB/radio mode change or flash operation is performed. */
void m1_battery_hal_init(void);
void m1_battery_hal_service(uint32_t now_ms);
const m1_battery_t *m1_battery_hal_status(void);
#endif
