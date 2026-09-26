#include "m1_startup.h"
#include "m1_hal.h"
#include "m1_lighting.h"
#include "m1_power_gpio.h"
#include "m1_usb_power.h"
#include "m1_sleep.h"
#include "m1_sleep_time.h"
#include "defaults.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

enum { OFF, WAIT_PB6, WAIT_PB12, WAIT_PC14, SCAN_STAMP, WAIT_SCAN,
       BAT_SLEEP_STAMP, BAT_SLEEP, BAT_RAILS, BAT_STAMP, BAT_SETTLE, BAT_CAPTURE,
       BAT_RESTORE, BAT_PC14, READY, FAILED, CLOCK_FATAL };
static unsigned state;
static uint32_t since;
static bool pins_owned,scan_owned,led_owned,gpio_owned,battery_path;
static uint8_t encoder_phase;

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
    if(state==CLOCK_FATAL)return;
    if(scan_owned)m1_hal_stop();
    if(led_owned)m1_lighting_stop();
    scan_owned=led_owned=false;
    if(pins_owned) {
        gpio_bits_reset(GPIOC,GPIO_PINS_6);
        gpio_bits_reset(GPIOB,GPIO_PINS_13|GPIO_PINS_6);
        gpio_bits_reset(GPIOC,GPIO_PINS_14);
    }
    pins_owned=false;
    if(gpio_owned && m1_power_gpio_restore(true,&encoder_phase))gpio_owned=false;
    state=OFF;
}
static void fail(void) { m1_startup_stop(); state=FAILED; }
bool m1_startup_begin(uint32_t now_ms,bool platform_quiescent)
{
    if(!platform_quiescent || state!=OFF || m1_power_gpio_prepared() ||
       __get_IPSR() || __get_BASEPRI() || __get_FAULTMASK() || (__get_CONTROL()&1u))return false;
    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);
    if(clocks.sclk_freq!=M1_CORE_HZ || clocks.ahb_freq!=M1_CORE_HZ ||
       clocks.apb1_freq!=M1_CORE_HZ/2u || clocks.apb2_freq!=M1_CORE_HZ) {
        state=FAILED; return false;
    }
    /* Shared cold/wake GPIO setup checks live USB/DMA/ADC before any pin writes. */
    if(!m1_power_gpio_restore(true,&encoder_phase)) { state=FAILED;return false; }
    gpio_init_type gpio;
    gpio_default_para_init(&gpio);
    gpio.gpio_pins=GPIO_PINS_13; gpio.gpio_mode=GPIO_MODE_INPUT;
    gpio.gpio_pull=GPIO_PULL_UP; gpio_init(GPIOC,&gpio);
    battery_path=!wired();
    /* Establish the reference cold-start low latches before enabling outputs.
     * PB12's full board function is not inferred; retain its observed low. */
    outputs(GPIOB,GPIO_PINS_6|GPIO_PINS_12|GPIO_PINS_13);
    outputs(GPIOC,GPIO_PINS_6|GPIO_PINS_14);
    pins_owned=true;
    since=now_ms;
    if(battery_path) {
        if(!m1_sleep_init() ||
           (!m1_sleep_time_ready() && !m1_sleep_time_begin()) ||
           m1_usb_power_down(true)!=M1_USB_POWER_OK ||
           !m1_power_gpio_prepare(true)) { fail();return false; }
        gpio_owned=true;state=BAT_SLEEP_STAMP;
    } else { gpio_bits_set(GPIOB,GPIO_PINS_6);state=WAIT_PB6; }
    return true;
}
bool m1_startup_fault(void) { return state==FAILED || state==CLOCK_FATAL; }
bool m1_startup_clock_fatal(void) { return state==CLOCK_FATAL; }
bool m1_startup_encoder_phase(uint8_t *phase)
{ if(!phase || state!=READY)return false;*phase=encoder_phase;return true; }
bool m1_startup_ready(void)
{
    return state==READY && m1_hal_healthy() && m1_lighting_healthy();
}
void m1_startup_service(uint32_t now_ms,uint32_t now_us)
{
    if(state==OFF || state==FAILED || state==CLOCK_FATAL)return;
    if(wired()==battery_path) { fail(); return; }
    if(led_owned)m1_lighting_service(now_us);
    if(state==READY) {
        if(!m1_hal_healthy() || !m1_lighting_healthy())fail();
        return;
    }
    if(state==BAT_SLEEP_STAMP) {
        m1_sleep_time_service();
        if(m1_sleep_time_fault()) { fail();return; }
        if(!m1_sleep_time_ready())return;
        since=now_ms;state=BAT_SLEEP;return;
    }
    /* Do not measure settling from timestamps captured before WFI or a
     * bounded ADC-calibration call: start the interval on a fresh poll. */
    if(state==BAT_RAILS) {
        gpio_bits_set(GPIOC,GPIO_PINS_14);gpio_bits_set(GPIOB,GPIO_PINS_6);
        scan_owned=true;
        if(!m1_hal_init()) { fail();return; }
        gpio_bits_set(GPIOC,GPIO_PINS_6);state=BAT_STAMP;return;
    }
    if(state==BAT_STAMP) { since=now_us;state=BAT_SETTLE;return; }
    if(state==BAT_SETTLE) {
        if((uint32_t)(now_us-since)<M1_COLD_SCAN_SETTLE_US)return;
        if(!m1_hal_capture_start(now_us)) { fail();return; }
        state=BAT_CAPTURE;return;
    }
    if(state==BAT_CAPTURE) {
        m1_hal_service(now_us);
        if(!m1_hal_healthy()) { fail();return; }
        if(m1_hal_capture_busy())return;
        uint16_t warmup[M1_KEY_COUNT];uint32_t sequence;
        if(!m1_hal_frame(warmup,&sequence)) { fail();return; }
        /* Startup acquisition is discarded, never a velocity or key event. */
        m1_hal_stop();scan_owned=false;
        gpio_bits_reset(GPIOB,GPIO_PINS_6);gpio_bits_reset(GPIOC,GPIO_PINS_14);
        gpio_bits_reset(GPIOC,GPIO_PINS_6);gpio_bits_set(GPIOB,GPIO_PINS_6);
        since=now_ms;state=BAT_RESTORE;return;
    }
    if(state==SCAN_STAMP) { since=now_ms;state=WAIT_SCAN;return; }
    uint32_t wait=state==WAIT_SCAN?M1_SENSOR_SETTLE_MS:M1_POWER_STAGE_MS;
    if((uint32_t)(now_ms-since)<wait)return;
    since=now_ms;
    switch(state) {
    case BAT_SLEEP: {
        gpio_bits_reset(GPIOC,GPIO_PINS_6);gpio_bits_reset(GPIOB,GPIO_PINS_6);
        gpio_bits_reset(GPIOC,GPIO_PINS_14);
        m1_sleep_result_t result=m1_sleep_timed_wait(M1_COLD_SLEEP_TICKS,true);
        if(result==M1_SLEEP_CLOCK_FATAL) { state=CLOCK_FATAL;return; }
        if(result!=M1_SLEEP_TIMER && result!=M1_SLEEP_OTHER_WAKE) { fail();break; }
        state=BAT_RAILS;break; /* refresh time after wake before raising rails */
    }
    case BAT_RESTORE:
        if(!m1_power_gpio_restore(true,&encoder_phase)) { fail();break; }
        gpio_owned=false;state=BAT_PC14;break;
    case BAT_PC14:
        gpio_bits_set(GPIOC,GPIO_PINS_14);state=WAIT_PC14;break;
    case WAIT_PB6:
        gpio_bits_reset(GPIOB,GPIO_PINS_12); state=WAIT_PB12; break;
    case WAIT_PB12:
        gpio_bits_set(GPIOC,GPIO_PINS_14); state=WAIT_PC14; break;
    case WAIT_PC14:
        scan_owned=true;
        if(!m1_hal_init()) { fail(); break; }
        gpio_bits_set(GPIOB,GPIO_PINS_13);
        led_owned=true;
        if(!m1_lighting_init(now_us)) { fail(); break; }
        gpio_bits_set(GPIOC,GPIO_PINS_6); state=SCAN_STAMP; break;
    case WAIT_SCAN:
        if(!m1_hal_start()) { fail(); break; }
        state=READY; break;
    default: fail(); break;
    }
}
