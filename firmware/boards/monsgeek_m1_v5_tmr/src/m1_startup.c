#include "m1_startup.h"
#include "m1_hal.h"
#include "m1_lighting.h"
#include "defaults.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

enum { OFF, WAIT_PB6, WAIT_PB12, WAIT_PC14, WAIT_SCAN, READY, FAILED };
static unsigned state;
static uint32_t since;
static bool pins_owned,scan_owned,led_owned;

static bool wired(void) { return gpio_input_data_bit_read(GPIOC,GPIO_PINS_13)==RESET; }
static void outputs(gpio_type *port,uint16_t pins)
{
    gpio_bits_reset(port,pins);
    gpio_init_type gpio;
    gpio_default_para_init(&gpio);
    gpio.gpio_pins=pins; gpio.gpio_mode=GPIO_MODE_OUTPUT;
    gpio.gpio_out_type=GPIO_OUTPUT_PUSH_PULL; gpio.gpio_pull=GPIO_PULL_NONE;
    gpio.gpio_drive_strength=GPIO_DRIVE_STRENGTH_MODERATE;
    gpio_init(port,&gpio);
}
void m1_startup_stop(void)
{
    if(scan_owned)m1_hal_stop();
    if(led_owned)m1_lighting_stop();
    if(pins_owned) {
        gpio_bits_reset(GPIOC,GPIO_PINS_6);
        gpio_bits_reset(GPIOB,GPIO_PINS_13|GPIO_PINS_6);
        gpio_bits_reset(GPIOC,GPIO_PINS_14);
    }
    state=OFF;
}
static void fail(void) { m1_startup_stop(); state=FAILED; }
bool m1_startup_begin(uint32_t now_ms)
{
    if(state!=OFF)return false;
    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);
    if(clocks.sclk_freq!=M1_CORE_HZ || clocks.ahb_freq!=M1_CORE_HZ ||
       clocks.apb1_freq!=M1_CORE_HZ/2u || clocks.apb2_freq!=M1_CORE_HZ) {
        state=FAILED; return false;
    }
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK,TRUE);
    gpio_init_type gpio;
    gpio_default_para_init(&gpio);
    gpio.gpio_pins=GPIO_PINS_13; gpio.gpio_mode=GPIO_MODE_INPUT;
    gpio.gpio_pull=GPIO_PULL_UP; gpio_init(GPIOC,&gpio);
    if(!wired()) { state=FAILED; return false; }
    /* Establish the reference cold-start low latches before enabling outputs.
     * PB12's full board function is not inferred; retain its observed low. */
    outputs(GPIOB,GPIO_PINS_6|GPIO_PINS_12|GPIO_PINS_13);
    outputs(GPIOC,GPIO_PINS_6|GPIO_PINS_14);
    pins_owned=true;
    gpio_bits_set(GPIOB,GPIO_PINS_6);
    since=now_ms; state=WAIT_PB6;
    return true;
}
bool m1_startup_fault(void) { return state==FAILED; }
bool m1_startup_ready(void)
{
    return state==READY && m1_hal_healthy() && m1_lighting_healthy();
}
void m1_startup_service(uint32_t now_ms)
{
    if(state==OFF || state==FAILED)return;
    if(!wired()) { fail(); return; }
    if(led_owned)m1_lighting_service(now_ms*1000u);
    if(state==READY) {
        if(!m1_hal_healthy() || !m1_lighting_healthy())fail();
        return;
    }
    uint32_t wait=state==WAIT_SCAN?M1_SENSOR_SETTLE_MS:M1_POWER_STAGE_MS;
    if((uint32_t)(now_ms-since)<wait)return;
    since=now_ms;
    switch(state) {
    case WAIT_PB6:
        gpio_bits_reset(GPIOB,GPIO_PINS_12); state=WAIT_PB12; break;
    case WAIT_PB12:
        gpio_bits_set(GPIOC,GPIO_PINS_14); state=WAIT_PC14; break;
    case WAIT_PC14:
        scan_owned=true;
        if(!m1_hal_init()) { fail(); break; }
        gpio_bits_set(GPIOB,GPIO_PINS_13);
        led_owned=true;
        if(!m1_lighting_init(now_ms*1000u)) { fail(); break; }
        gpio_bits_set(GPIOC,GPIO_PINS_6); state=WAIT_SCAN; break;
    case WAIT_SCAN:
        if(!m1_hal_start()) { fail(); break; }
        state=READY; break;
    default: fail(); break;
    }
}
