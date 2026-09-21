#include "m1_battery_hal.h"
#include "m1_hal.h"
#include "m1_power_gpio.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

static m1_battery_t battery;
static uint32_t sequence,seen_at;
static bool initialized,seen;
void m1_battery_hal_init(void)
{
    m1_battery_init(&battery); seen=false;
    if(m1_power_gpio_prepared()) { initialized=false;return; }
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK,TRUE);
    gpio_init_type gpio;
    gpio_default_para_init(&gpio);
    gpio.gpio_mode=GPIO_MODE_INPUT; gpio.gpio_pull=GPIO_PULL_UP;
    gpio.gpio_pins=GPIO_PINS_10; gpio_init(GPIOB,&gpio);
    gpio.gpio_pins=GPIO_PINS_13; gpio_init(GPIOC,&gpio);
    initialized=true;
}
const m1_battery_t *m1_battery_hal_status(void) { return &battery; }
void m1_battery_hal_service(uint32_t now)
{
    if(!initialized)return;
    if(m1_power_gpio_prepared()) { m1_battery_invalidate(&battery);seen=false;return; }
    uint16_t adc; uint32_t current;
    if(!m1_hal_battery(&adc,&current)) {
        m1_battery_invalidate(&battery); seen=false; return;
    }
    if(seen && current==sequence) {
        if((uint32_t)(now-seen_at)>=SCAN_STALE_MS)m1_battery_invalidate(&battery);
        return;
    }
    seen=true; sequence=current; seen_at=now;
    (void)m1_battery_sample(&battery,adc,
        gpio_input_data_bit_read(GPIOC,GPIO_PINS_13)==SET,
        gpio_input_data_bit_read(GPIOB,GPIO_PINS_10)==SET,now);
}
