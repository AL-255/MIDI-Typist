#include "m1_source.h"
#include "m1_live.h"
#include "m1_hal.h"
#include "m1_lighting.h"
#include "m1_wireless.h"
#include "m1_usb_hal.h"
#include "m1_usb_power.h"
#include "m1_sleep_time.h"
#include "m1_battery_hal.h"
#include "defaults.h"

enum { OFF,DRAIN,STABLE,PHY,RTC,STAMP,LINK,RESTORE,READY,FAILED };
static unsigned state;
static uint32_t started,stable_at,error;
static bool candidate,selected,radio_starting;
static m1_transport_t original,target,wireless;
uint32_t m1_source_error(void) { return error; }
bool m1_source_external(void) { return selected; }
static void fail(void) { error=state+1u;state=FAILED; }
static bool detach(uint32_t ms)
{
    if(m1_usb_hw_stop()!=M1_USB_HW_OK)return false;
    return m1_live_source_suspend(ms,true);
}
bool m1_source_begin(uint32_t ms,bool external,m1_transport_t fallback)
{
    if((state!=OFF && state!=READY) || !m1_transport_valid(fallback) || fallback==M1_TRANSPORT_USB)return false;
    original=m1_live_transport();wireless=fallback;candidate=external;
    started=stable_at=ms;radio_starting=false;error=0;state=DRAIN;
    if(!(external?m1_live_source_suspend(ms,false):detach(ms))) { fail();return false; }
    return true;
}
bool m1_source_begin_parked(uint32_t ms,bool external,m1_transport_t fallback)
{
    if(!m1_hal_healthy() || m1_hal_periodic_active() || m1_hal_capture_busy() ||
       m1_lighting_healthy() || !m1_wireless_healthy() || m1_wireless_sleep_sent() ||
       !m1_live_power_park())return false;
    if(!m1_source_begin(ms,external,fallback))return false;
    state=STABLE;return true;
}
m1_source_result_t m1_source_service(uint32_t ms,uint32_t us,bool external)
{
    if(state==FAILED || state==OFF)return M1_SOURCE_FAILED;
    if(state==READY)return M1_SOURCE_READY;
    if((uint32_t)(ms-started)>=M1_SOURCE_TRANSITION_MS) { fail();return M1_SOURCE_FAILED; }
    if(external!=candidate) {
        /* Debounce before PHY mutation. Once mutation starts, a second source
         * change is terminal, not permission to retry USB/rail initialization. */
        if(state>DRAIN && state!=STABLE) { fail();return M1_SOURCE_FAILED; }
        candidate=external;stable_at=ms;
        if(!external && !detach(ms)) { fail();return M1_SOURCE_FAILED; }
    }
    switch(state) {
    case DRAIN:
        m1_live_service(ms,us);
        if(!m1_hal_healthy() || !m1_lighting_healthy() ||
           (original!=M1_TRANSPORT_USB && !m1_wireless_healthy())) { fail();break; }
        if(!m1_live_power_park())break;
        if(!m1_hal_pause()) { fail();break; }
        m1_lighting_stop();state=STABLE;break;
    case STABLE:
        /* USB typing may leave a parked radio scheduler polling. A DMA
         * already in flight must still be serviced during cable debounce. */
        if(m1_wireless_healthy()) {
            m1_wireless_service(us);
            if(!m1_wireless_healthy()) { fail();break; }
        }
        if((uint32_t)(ms-stable_at)<M1_SOURCE_DEBOUNCE_MS)break;
        selected=candidate;target=original==M1_TRANSPORT_USB && !selected?wireless:original;
        state=PHY;break;
    case PHY:
        /* A parked scheduler can still own a battery/poll transaction. Finish
         * it before SDK initialization masks interrupts; do not restart it. */
        if(m1_wireless_healthy()) {
            m1_wireless_service(us);
            if(!m1_wireless_healthy()) { fail();break; }
            if(!m1_radio_ready())break;
        }
        if(selected) {
            if(!m1_usb_hw_running() && m1_usb_hw_start(true)!=M1_USB_HW_OK) { fail();break; }
            state=STAMP;
        } else {
            if(m1_usb_hw_running() || m1_usb_power_down(true)!=M1_USB_POWER_OK ||
               !m1_sleep_init() || (!m1_sleep_time_ready() && !m1_sleep_time_begin())) { fail();break; }
            state=RTC;
        }
        break;
    case RTC:
        m1_sleep_time_service();
        if(m1_sleep_time_fault()) { fail();break; }
        if(m1_sleep_time_ready())state=STAMP;
        break;
    case STAMP:
        /* Fresh timestamps after blocking USB/RTC setup; rails remain on. */
        if(!m1_lighting_init(us)) { fail();break; }
        if(target!=M1_TRANSPORT_USB && !m1_wireless_healthy()) {
            /* A failed established scheduler is not a cold radio. Never
             * turn a cable edge into an automatic retry that erases faults. */
            if(m1_wireless_errors()) { fail();break; }
            if(!m1_radio_init(us)) { fail();break; }
            radio_starting=true;
        }
        state=LINK;break;
    case LINK:
        m1_lighting_service(us);
        if(!m1_lighting_healthy()) { fail();break; }
        if(radio_starting) {
            m1_radio_service(us);
            if(!m1_radio_healthy()) { fail();break; }
            if(!m1_radio_ready())break;
            if(!m1_wireless_init(target,true,us)) { fail();break; }
            radio_starting=false;
        }
        if(target!=M1_TRANSPORT_USB) {
            m1_wireless_service(us);
            if(!m1_wireless_healthy()) { fail();break; }
            m1_transport_t current;
            if(!m1_wireless_mode(&current)) { fail();break; }
            if(current!=target) {
                if(!m1_wireless_switch_ready())break;
                if(!m1_wireless_select(target,us)) { fail();break; }
            }
            if(!m1_wireless_selected(target))break;
        }
        if(m1_lighting_ready())state=RESTORE;
        break;
    case RESTORE:
        if(selected && !m1_usb_hw_running()) { fail();break; }
        m1_battery_hal_init();
        if(!m1_hal_resume() || !m1_live_source_resume(ms,target,true)) { fail();break; }
        state=READY;break;
    default:fail();break;
    }
    return state==FAILED?M1_SOURCE_FAILED:state==READY?M1_SOURCE_READY:M1_SOURCE_RUNNING;
}
