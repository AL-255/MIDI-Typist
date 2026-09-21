#include "m1_power.h"
#include "m1_radio.h"

void m1_power_init(m1_power_t *s,uint16_t bluetooth_idle,uint16_t radio_idle)
{ *s=(m1_power_t){.bluetooth_idle=bluetooth_idle,.radio_idle=radio_idle}; }
void m1_power_activity(m1_power_t *s)
{
    s->elapsed=0; s->periodic=0;
    s->sleep_requested=s->radio_committed=false; s->radio_command=0;
}
void m1_power_woke(m1_power_t *s) { m1_power_activity(s); }
void m1_power_tick(m1_power_t *s,const m1_power_input_t *in)
{
    if(!in || !m1_transport_valid(in->transport) || in->blocked) {
        m1_power_activity(s); return;
    }
    if(in->externally_powered || in->transport==M1_TRANSPORT_USB) {
        s->critical_latched=false; m1_power_activity(s); return;
    }
    if(in->battery && m1_battery_critical(in->battery))s->critical_latched=true;
    if(in->activity) {
        /* Held keys/encoder motion inhibit ordinary idle, but cannot bypass
         * the reference's latched critical-battery protection indefinitely. */
        if(!s->critical_latched) { m1_power_activity(s); return; }
    }
    if(s->sleep_requested) {
        /* Critical protection supersedes BT retention even if that earlier
         * command already completed. Completion of 5 cannot authorize 3. */
        if(s->critical_latched && s->radio_command!=M1_RADIO_SLEEP) {
            s->radio_command=M1_RADIO_SLEEP;s->radio_committed=false;
        }
        return;
    }
    unsigned ticks=in->fast_idle?M1_POWER_FAST_QUALIFY_TICKS:M1_POWER_QUALIFY_TICKS;
    if(++s->periodic<ticks)return;
    s->periodic=0;
    uint32_t limit;
    if(s->critical_latched)limit=M1_POWER_CRITICAL_STEPS;
    else switch(in->selector) {
        case 1: limit=in->transport==M1_TRANSPORT_RADIO?s->radio_idle:s->bluetooth_idle; break;
        case 2: limit=in->transport==M1_TRANSPORT_RADIO?M1_POWER_RADIO_SEARCH_STEPS:M1_POWER_BT_SEARCH_STEPS; break;
        case 3: limit=M1_POWER_UNSELECTED_STEPS; break;
        default: return;
    }
    if(s->elapsed!=UINT32_MAX)++s->elapsed;
    if(!limit || s->elapsed<limit)return;
    s->elapsed=0; s->sleep_requested=true; s->radio_committed=false;
    /* Opcode 0x94 payload 5 retains BT's intermediate radio state; all other
     * paths request payload 3. The later RTC/rail sequence is a separate HAL. */
    s->radio_command=!s->critical_latched && in->selector==1 &&
                     in->transport!=M1_TRANSPORT_RADIO?M1_RADIO_BT_RETAIN:M1_RADIO_SLEEP;
}
bool m1_power_radio_committed(m1_power_t *s,uint8_t command)
{
    if(!s->sleep_requested || command!=s->radio_command)return false;
    s->radio_committed=true; return true;
}
bool m1_power_can_sleep(const m1_power_t *s,bool reports_drained,bool scan_stopped,
                        bool leds_off,bool radio_idle,bool usb_quiescent)
{
    return s->sleep_requested && s->radio_committed && reports_drained &&
           scan_stopped && leds_off && radio_idle && usb_quiescent;
}
