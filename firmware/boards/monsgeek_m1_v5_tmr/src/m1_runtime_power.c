#include "m1_runtime_power.h"
#include "m1_source.h"
#include "m1_live.h"
#include "m1_power.h"
#include "m1_power_gpio.h"
#include "m1_sleep_time.h"
#include "m1_wake.h"
#include "m1_hal.h"
#include "m1_lighting.h"
#include "m1_wireless.h"
#include "m1_usb_hal.h"
#include "m1_usb_power.h"
#include "m1_battery_hal.h"
#include "defaults.h"
#include "at32f402_405_conf.h"

static m1_runtime_power_state_t state;
static m1_power_t policy;
static m1_wake_t wake;
static bool initialized,retained,scan_initialized,source_external;
static uint32_t last_tick,since,scan_stamp,retained_at,error;
static uint8_t switches;
static m1_transport_t transport;
static m1_transport_t last_wireless=M1_DEFAULT_WIRELESS_TRANSPORT;
static const uint8_t black[M1_LED_BYTES]={0};

m1_runtime_power_state_t m1_runtime_power_state(void) { return state; }
uint32_t m1_runtime_power_error(void) { return error; }
static void fail(void) { error=(uint32_t)state+1u;state=M1_RUNTIME_FAILED; }
static void source_failed(void) { fail();error|=m1_source_error()<<8u; }
static void enter(m1_runtime_power_state_t next,uint32_t now)
{ state=next;since=now; }
static void rails_off(void)
{
    gpio_bits_reset(GPIOC,GPIO_PINS_6);
    gpio_bits_reset(GPIOB,GPIO_PINS_6);
    gpio_bits_reset(GPIOC,GPIO_PINS_14);
}
/* Reference 0x08017a34 state table, not an inference from a friendly RF name.
 * Selector 2's separate pairing requests remain outside this sleep owner. */
static uint8_t selector(m1_transport_t mode,uint8_t peer)
{
    if(peer==3)return 1;
    if(peer==4 || (mode==M1_TRANSPORT_BT1 && peer==2))return 2;
    if(peer<2 || (mode==M1_TRANSPORT_RADIO && peer==2))return 3;
    return 0;
}
static bool quiesce_peer(void)
{
    if(!m1_radio_quiesce(true))return false;
    m1_wireless_stop();retained=false;return true;
}
static void restore(uint32_t now)
{
    m1_hal_stop();scan_initialized=false;rails_off();
    gpio_bits_set(GPIOB,GPIO_PINS_6);
    enter(M1_RUNTIME_RESTORE_GPIO,now);
}
static bool external_now(void)
{ return gpio_input_data_bit_read(GPIOC,GPIO_PINS_13)==RESET; }
static void cancel_sleep(uint32_t ms)
{
    /* No peer sleep transaction was started, so do not reset the peer or
     * cycle sensor rails. Acquisition is still paused, with RAM intact. */
    m1_lighting_stop();gpio_bits_set(GPIOB,GPIO_PINS_13);
    enter(M1_RUNTIME_SOURCE,ms);
    if(!m1_source_begin_parked(ms,true,last_wireless))source_failed();
}
void m1_runtime_power_service(uint32_t ms,uint32_t us,bool external)
{
    if(state==M1_RUNTIME_FAILED || state==M1_RUNTIME_CLOCK_FATAL || state==M1_RUNTIME_TIME_FATAL)return;
    if(!initialized) {
        m1_power_init(&policy,M1_RUNTIME_BT_IDLE_STEPS,M1_RUNTIME_RADIO_IDLE_STEPS);
        initialized=true;last_tick=ms;
        /* Boot owns USB exactly when it completed an externally powered
         * startup. Do not miss a cable edge just before this first service. */
        source_external=m1_usb_hw_running();
    }
    if(state==M1_RUNTIME_SOURCE) {
        m1_source_result_t result=m1_source_service(ms,us,external);
        if(result==M1_SOURCE_FAILED) { source_failed();return; }
        if(result==M1_SOURCE_READY) {
            source_external=m1_source_external();
            m1_power_woke(&policy);last_tick=ms;enter(M1_RUNTIME_AWAKE,ms);
        }
        return;
    }
    if(external!=source_external) {
        if(state==M1_RUNTIME_AWAKE || state==M1_RUNTIME_DRAIN) {
            transport=m1_live_transport();
            if(transport!=M1_TRANSPORT_USB)last_wireless=transport;
            enter(M1_RUNTIME_SOURCE,ms);
            if(!m1_source_begin(ms,external,last_wireless))source_failed();
            return;
        }
        /* Sleep is battery-only. A cable arrival is a wake reason, not a
         * reason to reinitialize live or resume an unconfirmed radio peer.
         * BLANK/PEER/DEEPEN finish or cancel their specific transaction below;
         * restoration itself finishes with USB still stopped. The following
         * awake turn then gives the source owner its ordinary paused handoff. */
        if(!external) { fail();return; }
        if(state==M1_RUNTIME_SETTLE || state==M1_RUNTIME_SLEEP ||
           state==M1_RUNTIME_SCAN_STAMP || state==M1_RUNTIME_SCAN_SETTLE ||
           state==M1_RUNTIME_CAPTURE) {
            if((state==M1_RUNTIME_CAPTURE || state==M1_RUNTIME_SCAN_SETTLE) &&
               !m1_hal_healthy()) { fail();return; }
            restore(ms);return;
        }
    }
    if(state==M1_RUNTIME_AWAKE) {
        m1_live_service(ms,us);
        transport=m1_live_transport();
        if(transport!=M1_TRANSPORT_USB)last_wireless=transport;
        if((uint32_t)(ms-last_tick)<M1_RUNTIME_POWER_PERIOD_MS)return;
        /* One observation per elapsed slot, not a burst of synthetic ticks
         * after a flash pause or a late foreground service. */
        last_tick=ms;
        bool activity=false;
        bool eligible=m1_live_power_activity(&activity);
        m1_radio_status_t peer;
        uint8_t selection=m1_wireless_status(&peer)?selector(transport,peer.state):0;
        m1_power_input_t input={.transport=transport,.selector=selection,
            .externally_powered=external,.blocked=!eligible,.activity=activity,
            .battery=m1_battery_hal_status()};
        m1_power_tick(&policy,&input);
        if(!policy.sleep_requested)return;
        if(!m1_sleep_time_ready() || !m1_live_power_suspend(ms)) { fail();return; }
        enter(M1_RUNTIME_DRAIN,ms);return;
    }
    if(state!=M1_RUNTIME_SLEEP && state!=M1_RUNTIME_CAPTURE &&
       (uint32_t)(ms-since)>=M1_RUNTIME_HANDOFF_MS) { fail();return; }
    switch(state) {
    case M1_RUNTIME_DRAIN:
        m1_live_service(ms,us);
        if(!m1_hal_healthy() || !m1_lighting_healthy() || !m1_wireless_healthy()) { fail();break; }
        if(!m1_live_power_park())break;
        /* Stop acquisition immediately after foreground ownership transfers;
         * neither LED drain nor peer control may leave its FIFO filling. */
        if(!m1_hal_pause() || !m1_lighting_offer(black,sizeof(black),us)) { fail();break; }
        enter(M1_RUNTIME_BLANK,ms);break;
    case M1_RUNTIME_BLANK:
        m1_lighting_service(us);m1_wireless_service(us);
        if(!m1_lighting_healthy() || !m1_wireless_healthy()) { fail();break; }
        if(!m1_lighting_ready())break;
        if(external) { cancel_sleep(ms);break; }
        if(!m1_wireless_request_sleep(policy.radio_command,true))break;
        m1_lighting_stop();gpio_bits_reset(GPIOB,GPIO_PINS_13);
        enter(M1_RUNTIME_PEER,ms);break;
    case M1_RUNTIME_PEER:
        /* Cancel before service can submit a queued sleep packet. False
         * means it may already be in flight: finish that exact packet. */
        if(external && m1_wireless_cancel_sleep()) { cancel_sleep(ms);break; }
        m1_wireless_service(us);
        if(!m1_wireless_healthy()) { fail();break; }
        if(m1_wireless_sleep_sent()!=policy.radio_command)break;
        if(!m1_power_radio_committed(&policy,policy.radio_command)) { fail();break; }
        retained=policy.radio_command==M1_RADIO_BT_RETAIN;retained_at=ms;
        if(!retained && !quiesce_peer()) { fail();break; }
        if(external) { restore(ms);break; }
        m1_hal_stop();scan_initialized=false;rails_off();
        if(m1_usb_hw_running()) { fail();break; }
        m1_usb_power_result_t reduced=m1_usb_power_down(true);
        if(reduced==M1_USB_POWER_EXTERNAL) { restore(ms);break; }
        if(reduced!=M1_USB_POWER_OK) { fail();break; }
        if(!m1_power_gpio_prepare(true)) {
            /* PC13 may change inside the HAL's guarded power check. Other
             * failures remain terminal; no blind retry of GPIO/PHY setup. */
            if(external_now())restore(ms);else fail();
            break;
        }
        if(!m1_power_gpio_switches(&switches)) { fail();break; }
        m1_wake_init(&wake,NULL);enter(M1_RUNTIME_SETTLE,ms);break;
    case M1_RUNTIME_SETTLE:
        if((uint32_t)(ms-since)>=M1_RUNTIME_SLEEP_SETTLE_MS)enter(M1_RUNTIME_SLEEP,ms);
        break;
    case M1_RUNTIME_SLEEP: {
        if(retained && (uint32_t)(ms-retained_at)>=M1_RUNTIME_BT_RETAIN_MS) {
            m1_wireless_service(us);
            if(!m1_wireless_request_sleep(M1_RADIO_SLEEP,true)) { fail();break; }
            policy.radio_command=M1_RADIO_SLEEP;policy.radio_committed=false;
            enter(M1_RUNTIME_DEEPEN,ms);break;
        }
        rails_off();
        bool radio_idle=!retained || (m1_radio_ready() && m1_wireless_sleep_sent()==M1_RADIO_BT_RETAIN);
        bool safe=m1_power_can_sleep(&policy,true,!m1_hal_periodic_active() && !m1_hal_capture_busy(),
                                    !m1_lighting_healthy(),radio_idle,m1_usb_power_ready());
        m1_sleep_result_t result=m1_sleep_timed_wait(M1_RUNTIME_SLEEP_TICKS,safe);
        if(result==M1_SLEEP_CLOCK_FATAL || result==M1_SLEEP_TIME_ERROR) {
            error=(uint32_t)state+1u;
            state=result==M1_SLEEP_CLOCK_FATAL?M1_RUNTIME_CLOCK_FATAL:M1_RUNTIME_TIME_FATAL;break;
        }
        if(result==M1_SLEEP_BUSY && !safe && external_now()) { restore(ms);break; }
        if(result!=M1_SLEEP_TIMER && result!=M1_SLEEP_OTHER_WAKE) { fail();break; }
        /* No time-sensitive work with timestamps sampled before WFI. */
        state=M1_RUNTIME_SCAN_STAMP;break;
    }
    case M1_RUNTIME_SCAN_STAMP:
        gpio_bits_set(GPIOC,GPIO_PINS_14);gpio_bits_set(GPIOB,GPIO_PINS_6);
        if(!scan_initialized) {
            if(!m1_hal_init()) { fail();break; }
            scan_initialized=true;
            /* ADC calibration may block: stamp settling on the next call. */
            break;
        }
        gpio_bits_set(GPIOC,GPIO_PINS_6);scan_stamp=us;
        enter(M1_RUNTIME_SCAN_SETTLE,ms);break;
    case M1_RUNTIME_SCAN_SETTLE:
        if((uint32_t)(us-scan_stamp)<M1_RUNTIME_SCAN_SETTLE_US)break;
        if(!m1_hal_capture_start(us)) { fail();break; }
        enter(M1_RUNTIME_CAPTURE,ms);break;
    case M1_RUNTIME_CAPTURE: {
        m1_hal_service(us);
        if(!m1_hal_healthy()) { fail();break; }
        if(m1_hal_capture_busy())break;
        uint16_t frame[M1_KEY_COUNT];uint32_t sequence;uint8_t current;
        if(!m1_hal_frame(frame,&sequence) || !m1_power_gpio_switches(&current)) { fail();break; }
        m1_wake_result_t reason=m1_wake_frame(&wake,frame,sequence);
        if(reason==M1_WAKE_FAULT) { fail();break; }
        if(reason==M1_WAKE_KEYS || current!=switches)restore(ms);
        else enter(M1_RUNTIME_SLEEP,ms);
        break;
    }
    case M1_RUNTIME_DEEPEN:
        if(external && m1_wireless_cancel_sleep()) { restore(ms);break; }
        m1_wireless_service(us);
        if(!m1_wireless_healthy()) { fail();break; }
        if(m1_wireless_sleep_sent()!=M1_RADIO_SLEEP)break;
        if(!m1_power_radio_committed(&policy,M1_RADIO_SLEEP) || !quiesce_peer()) { fail();break; }
        if(external)restore(ms);else enter(M1_RUNTIME_SLEEP,ms);
        break;
    case M1_RUNTIME_RESTORE_GPIO: {
        if((uint32_t)(ms-since)<M1_RUNTIME_RESTORE_STAGE_MS)break;
        uint8_t phase;
        if(!m1_power_gpio_restore(true,&phase)) { fail();break; }
        enter(M1_RUNTIME_RESTORE_RAILS,ms);break;
    }
    case M1_RUNTIME_RESTORE_RAILS:
        if((uint32_t)(ms-since)<M1_RUNTIME_RESTORE_STAGE_MS)break;
        gpio_bits_set(GPIOC,GPIO_PINS_14);
        if(!m1_hal_init()) { fail();break; }
        gpio_bits_set(GPIOB,GPIO_PINS_13);
        if(!m1_lighting_init(us)) { fail();break; }
        gpio_bits_set(GPIOC,GPIO_PINS_6);state=M1_RUNTIME_RESTORE_STAMP;break;
    case M1_RUNTIME_RESTORE_STAMP:
        enter(M1_RUNTIME_RESTORE_SETTLE,ms);break;
    case M1_RUNTIME_RESTORE_SETTLE:
        m1_lighting_service(us);
        if(!m1_lighting_healthy()) { fail();break; }
        if((uint32_t)(ms-since)<M1_RUNTIME_RESTORE_SETTLE_MS || !m1_lighting_ready())break;
        if(retained) {
            if(!m1_wireless_resume_retained(true,us)) { fail();break; }
            enter(M1_RUNTIME_RESTORE_LINK,ms);
        } else {
            if(!m1_radio_init(us)) { fail();break; }
            enter(M1_RUNTIME_RESTORE_RADIO,ms);
        }
        break;
    case M1_RUNTIME_RESTORE_RADIO:
        m1_radio_service(us);
        if(!m1_radio_healthy()) { fail();break; }
        if(!m1_radio_ready())break;
        if(!m1_wireless_init(transport,true,us)) { fail();break; }
        enter(M1_RUNTIME_RESTORE_LINK,ms);break;
    case M1_RUNTIME_RESTORE_LINK:
        m1_wireless_service(us);
        if(!m1_wireless_healthy() || !m1_lighting_healthy()) { fail();break; }
        if(!m1_wireless_selected(transport))break;
        m1_battery_hal_init();
        if(!m1_hal_start() || !m1_live_power_resume(ms,true)) { fail();break; }
        m1_power_woke(&policy);last_tick=ms;enter(M1_RUNTIME_AWAKE,ms);break;
    default:fail();break;
    }
}
