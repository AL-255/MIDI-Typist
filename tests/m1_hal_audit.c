/* Link-only test exports. No startup, vector table, boot header or flash image.
 * The harness loads this synthetic-address ELF into Unicorn, never a device. */
#include "m1_lighting.h"
#include "m1_hal.h"
#include "m1_startup.h"
#include "m1_battery_hal.h"
#include "m1_sleep.h"
#include "m1_radio.h"
#include "m1_wireless.h"
#include "m1_usb_power.h"
uint32_t m1_test_battery_status(void)
{
    const m1_battery_t *s=m1_battery_hal_status();
    return s->percent | ((uint32_t)s->valid<<8) | ((uint32_t)s->charger<<16);
}
__attribute__((used,section(".test_exports")))
const void *const m1_test_exports[]={
    m1_lighting_init,m1_lighting_stop,m1_lighting_offer,m1_lighting_service,
    m1_lighting_ready,m1_lighting_healthy,m1_lighting_errors,
    m1_hal_init,m1_hal_start,m1_hal_stop,m1_hal_timer_irq,m1_hal_dma_irq,
    m1_hal_frame,m1_hal_battery,m1_hal_healthy,m1_hal_periodic_active,m1_hal_errors,
    m1_hal_capture_start,m1_hal_capture_busy,m1_hal_service,
    m1_clock_init,m1_startup_begin,m1_startup_service,m1_startup_stop,
    m1_startup_ready,m1_startup_fault,
    m1_battery_hal_init,m1_battery_hal_service,m1_test_battery_status,
    m1_sleep_init,m1_sleep_ready,m1_sleep_wait,m1_sleep_irq,
    m1_radio_init,m1_radio_service,m1_radio_ready,m1_radio_healthy,m1_radio_errors,
    m1_radio_data_pending,m1_radio_exchange,m1_radio_take,m1_radio_stop,m1_radio_quiesce,
    m1_wireless_init,m1_wireless_service,m1_wireless_stop,m1_wireless_healthy,
    m1_wireless_ready,m1_wireless_offer,m1_wireless_local_idle,
    m1_wireless_reports_sent,m1_wireless_errors,m1_wireless_status,
    m1_usb_power_down,m1_usb_power_ready,m1_usb_power_invalidate
};
