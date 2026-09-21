/* Link-only test exports. No startup, vector table, boot header or flash image.
 * The harness loads this synthetic-address ELF into Unicorn, never a device. */
#include "m1_lighting.h"
#include "m1_hal.h"
#include "m1_startup.h"
#include "m1_battery_hal.h"
#include "m1_sleep.h"
#include "m1_radio.h"
#include "m1_wireless.h"
#include "m1_power.h"
#include "m1_usb_power.h"
uint32_t m1_test_battery_status(void)
{
    const m1_battery_t *s=m1_battery_hal_status();
    return s->percent | ((uint32_t)s->valid<<8) | ((uint32_t)s->charger<<16);
}
/* Compose actual filtering/policy with the scheduler; only ADC samples and
 * qualification calls are scripted. No production timing is inferred. */
uint32_t m1_test_wireless_battery(uint16_t adc,unsigned samples,unsigned pins)
{
    m1_battery_t battery;m1_battery_init(&battery);
    for(unsigned i=0;i<samples;++i)
        (void)m1_battery_sample(&battery,adc,pins&1u,pins&2u,i*M1_BATTERY_SAMPLE_MS);
    return ((uint32_t)battery.percent<<8)|m1_wireless_battery(&battery);
}
bool m1_test_wireless_battery_raw(uint8_t percent,unsigned flags)
{
    m1_battery_t battery={.percent=percent,.valid=flags&1u,.source_known=flags&2u};
    return m1_wireless_battery(&battery);
}
static m1_power_t wireless_power;
bool m1_test_wireless_power_request(unsigned mode,bool critical,bool host_released)
{
    m1_battery_t battery={.valid=true,.source_known=true,
        .percent=critical?M1_BATTERY_CRITICAL_PERCENT:100u};
    m1_power_input_t input={.transport=mode,.selector=1,.fast_idle=true,.battery=&battery};
    m1_power_init(&wireless_power,1,1);
    for(unsigned i=0;i<M1_POWER_CRITICAL_STEPS*M1_POWER_FAST_QUALIFY_TICKS;++i)
        m1_power_tick(&wireless_power,&input);
    return wireless_power.sleep_requested &&
        m1_wireless_request_sleep(wireless_power.radio_command,host_released);
}
uint32_t m1_test_wireless_power_status(unsigned guards)
{
    (void)m1_power_radio_committed(&wireless_power,m1_wireless_sleep_sent());
    return wireless_power.radio_command | ((uint32_t)wireless_power.radio_committed<<8) |
        ((uint32_t)m1_power_can_sleep(&wireless_power,guards&1u,guards&2u,guards&4u,
                                    guards&8u,guards&16u)<<9);
}
void m1_test_wireless_power_critical(void)
{
    m1_battery_t battery={.valid=true,.percent=M1_BATTERY_CRITICAL_PERCENT};
    m1_power_input_t input={.transport=M1_TRANSPORT_BT1,.selector=1,.battery=&battery};
    m1_power_tick(&wireless_power,&input);
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
    m1_wireless_battery,m1_wireless_battery_sent,m1_wireless_request_sleep,
    m1_wireless_cancel_sleep,m1_wireless_sleep_sent,m1_test_wireless_battery,
    m1_test_wireless_battery_raw,m1_test_wireless_power_request,m1_test_wireless_power_status,
    m1_test_wireless_power_critical,
    m1_usb_power_down,m1_usb_power_ready,m1_usb_power_invalidate
};
