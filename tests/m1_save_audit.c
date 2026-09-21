/* Actual pause/time/battery/LED HAL and save gate. Transport readiness is
 * scripted; actual class/radio scheduling has separate linked ARM audits.
 * No application image, real flash operation or physical power assertion. */
#include "m1_save.h"
#include "m1_hal.h"
#include "m1_time.h"
#include "m1_battery_hal.h"
#include "m1_lighting.h"
static unsigned mode=M1_TRANSPORT_USB,radio=M1_TRANSPORT_BT1,flags=3;
m1_transport_t __wrap_m1_live_transport(void) { return mode; }
bool __wrap_m1_usb_ready(void) { return flags&1u; }
bool __wrap_m1_usb_drained(void) { return flags&2u; }
bool __wrap_m1_wireless_healthy(void) { return flags&4u; }
bool __wrap_m1_wireless_ready(void) { return flags&8u; }
bool __wrap_m1_wireless_local_idle(void) { return flags&16u; }
bool __wrap_m1_wireless_mode(m1_transport_t *out)
{ if(!(flags&4u))return false;*out=radio;return true; }
void m1_test_save_transport(unsigned selected,unsigned configured,unsigned state)
{ mode=selected;radio=configured;flags=state; }
unsigned m1_test_save_begin(void) { return m1_save_ops()->begin(NULL); }
bool m1_test_save_end(void) { return m1_save_ops()->end(NULL); }
__attribute__((used,section(".test_exports")))
const void *const exports[]={m1_test_save_begin,m1_test_save_end,m1_test_save_transport,m1_save_fault,
    m1_hal_init,m1_hal_start,m1_hal_timer_irq,m1_hal_dma_irq,m1_hal_periodic_active,m1_hal_healthy,
    m1_hal_frame,m1_hal_battery,m1_hal_pause,m1_hal_resume,m1_hal_stop,
    m1_time_start,m1_time_now,m1_time_stop,m1_battery_hal_init,m1_battery_hal_service,
    m1_lighting_init,m1_lighting_service,m1_lighting_offer,m1_lighting_ready};
