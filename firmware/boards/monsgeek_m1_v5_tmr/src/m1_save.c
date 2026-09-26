#include "m1_save.h"
#include "m1_hal.h"
#include "m1_time.h"
#include "m1_lighting.h"
#include "m1_battery_hal.h"
#include "m1_power_gpio.h"
#include "m1_wireless.h"
#include "m1_usb.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"
#include "m1_radio.h"

static bool owned,faulted,external;
static uint32_t saved_mask,started;
static bool context(void)
{ return !__get_IPSR() && !__get_BASEPRI() && !__get_FAULTMASK() && !(__get_CONTROL()&1u); }
static bool output_pin(gpio_type *port,unsigned pin)
{ return ((port->cfgr>>(pin*2u))&3u)==GPIO_MODE_OUTPUT && (port->odt&(1u<<pin)); }
static bool power_pins(void)
{
    return !m1_power_gpio_prepared() &&
        ((GPIOC->cfgr>>(13u*2u))&3u)==GPIO_MODE_INPUT &&
        output_pin(GPIOB,6) && output_pin(GPIOB,13) &&
        output_pin(GPIOC,6) && output_pin(GPIOC,14);
}
static bool source(void) { return gpio_input_data_bit_read(GPIOC,GPIO_PINS_13)==RESET; }
static bool supply(uint32_t now_ms)
{
    const m1_battery_t *b=m1_battery_hal_status();
    if(!power_pins() || !b->source_known || !b->valid || !b->sample_clock ||
       b->charger==M1_CHARGER_UNKNOWN ||
       (uint32_t)(now_ms-b->sampled_at)>=M1_BATTERY_SAMPLE_MS*2u ||
       source()!=b->externally_powered)return false;
    if(b->externally_powered)return true;
    uint16_t raw;uint32_t sequence;
    /* Check the newest raw battery sample too: the display's slow monotonic
     * filter must not hide a sudden voltage drop during save qualification. */
    return b->percent>=M1_FLASH_MIN_BATTERY_PERCENT &&
        m1_hal_battery(&raw,&sequence) &&
        m1_battery_percent(raw)>=M1_FLASH_MIN_BATTERY_PERCENT;
}
static bool idle_bus(bool scanning)
{
    for(unsigned i=0;i<7;++i) {
        if((!scanning || i!=5) &&
           ((dma_channel_type *)(DMA1_CHANNEL1_BASE+i*0x14u))->ctrl_bit.chen)return false;
        if(((dma_channel_type *)(DMA2_CHANNEL1_BASE+i*0x14u))->ctrl_bit.chen)return false;
    }
    return !SPI2->sts_bit.bf && m1_radio_bus_idle() &&
        !(CRM->ahben1_bit.otghsen && OTG2_GLOBAL->gahbcfg_bit.dmaen) &&
        !(SysTick->CTRL&SysTick_CTRL_ENABLE_Msk);
}
static bool transports(void)
{
    m1_transport_t mode=m1_live_transport(),radio;
    if(!m1_transport_valid(mode))return false;
    if(mode==M1_TRANSPORT_USB) {
        if(!m1_usb_ready() || !m1_usb_drained())return false;
    } else if(!m1_wireless_mode(&radio) || radio!=mode ||
              !m1_wireless_ready() || !m1_wireless_local_idle())return false;
    /* A connected USB GUI or an initialized radio may coexist with the
     * selected keyboard transport. Do not abandon either local transfer. */
    if(m1_usb_ready() && !m1_usb_drained())return false;
    return !m1_wireless_healthy() || m1_wireless_local_idle();
}
static uint32_t blocked(void *unused)
{
    (void)unused;
    m1_time_point_t time;
    if(!m1_time_now(&time))return 1u;
    return (!m1_hal_periodic_active()?2u:0u) |
           (!m1_lighting_ready()?4u:0u) |
           (!supply(time.ms)?8u:0u) |
           (!transports()?16u:0u) |
           (!idle_bus(true)?32u:0u) |
           (!power_pins()?64u:0u) |
           ((!m1_hal_healthy() || !m1_lighting_healthy())?128u:0u);
}
static m1_save_result_t begin(void *unused)
{
    (void)unused;
    if(faulted || owned || !context()) { faulted=true;return M1_SAVE_FAULT; }
    m1_time_point_t time;
    if(!m1_time_now(&time) || !m1_hal_healthy() || !m1_lighting_healthy()) {
        faulted=true;return M1_SAVE_FAULT;
    }
    if(!m1_hal_periodic_active() || !m1_lighting_ready() ||
       !supply(time.ms) || !transports() || !idle_bus(true))return M1_SAVE_DEFER;
    /* Clock validation and whole-bus inspection are read-only preflight.
     * Do not hold off scan/DMA IRQs across those SDK/register walks. Refresh
     * the time after preflight, then recheck interrupt-sensitive readiness
     * before taking ownership. A USB transfer racing preflight must still
     * defer without stopping acquisition or discarding a calibration. */
    if(!m1_time_now(&time)) { faulted=true;return M1_SAVE_FAULT; }
    uint32_t mask=__get_PRIMASK();__disable_irq();
    if(!m1_hal_healthy() || !m1_lighting_healthy()) {
        faulted=true;__set_PRIMASK(mask);return M1_SAVE_FAULT;
    }
    if(!m1_hal_periodic_active() || !m1_lighting_ready() ||
       !supply(time.ms) || !transports()) {
        __set_PRIMASK(mask);return M1_SAVE_DEFER;
    }
    external=source();
    if(!m1_hal_pause()) {
        faulted=true;__set_PRIMASK(mask);return M1_SAVE_FAULT;
    }
    if(!idle_bus(false) || ADC1->ctrl2_bit.adcen || (TMR3->ctrl1&1u) ||
       (TMR6->ctrl1&1u) || !power_pins() || source()!=external) {
        /* The scanner is already stopped; never describe this as unchanged
         * deferral or retry an ambiguous peripheral/power transition. */
        faulted=true;__set_PRIMASK(mask);return M1_SAVE_FAULT;
    }
    saved_mask=mask;started=time.us;owned=true;
    return M1_SAVE_READY; /* IRQs remain masked across journal/SDK transaction. */
}
static bool end(void *unused)
{
    (void)unused;
    if(!owned || !context()) { faulted=true;return false; }
    m1_time_point_t time;
    bool valid=!faulted && __get_PRIMASK() && m1_time_now(&time) &&
        (uint32_t)(time.us-started)<=M1_FLASH_MAX_PAUSE_US &&
        power_pins() && source()==external && idle_bus(false) &&
        m1_hal_resume();
    owned=false;if(!valid)faulted=true;
    __DMB();__set_PRIMASK(saved_mask);return valid;
}
const m1_live_storage_ops_t *m1_save_ops(void)
{ static const m1_live_storage_ops_t ops={begin,end,NULL,blocked};return &ops; }
bool m1_save_fault(void) { return faulted; }
