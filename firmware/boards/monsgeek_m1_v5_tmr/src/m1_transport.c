#include "m1_transport.h"
#include "m1_live.h"
#include "m1_wireless.h"
#include "m1_usb.h"

static uint32_t now;
static bool starting,selecting,pairing;
static m1_transport_t requested;

void m1_transport_service(uint32_t now_us)
{
    now=now_us;
    /* The scheduler cannot service the HAL until it exists. Only this owner
     * services an initial radio pulse; live owns the established scheduler. */
    if(starting)m1_radio_service(now);
}
static bool available(void *context,m1_transport_t target)
{
    (void)context;
    if(target==M1_TRANSPORT_USB)return m1_usb_ready();
#if MT_M1_WIRELESS
    return true;
#else
    /* USB-only artifact: Fn+F1-F5 and any host request must never select a
     * wireless transport that this image cannot service. */
    return false;
#endif
}
static bool drained(void *context)
{
    (void)context;
    return m1_live_transport()==M1_TRANSPORT_USB?m1_usb_ready() && m1_usb_drained():
        m1_wireless_switch_ready();
}
static bool select_mode(void *context,m1_transport_t target)
{
    (void)context;
    if(!available(NULL,target))return false;
    if(!selecting) {
        requested=target;selecting=true;
        if(!m1_radio_healthy() && target!=M1_TRANSPORT_USB) {
            m1_wireless_stop(); /* explicit selection, never automatic link retry */
            if(!m1_radio_init(now))return false;
            starting=true;
        }
    }
    if(target!=requested)return false;
    if(starting) {
        if(!m1_radio_ready())return false;
        if(!m1_wireless_init(target,true,now))return false;
        starting=false;
    }
    m1_transport_t configured;
    if(m1_wireless_mode(&configured)) {
        if(configured!=target && !m1_wireless_select(target,now))return false;
        if(!m1_wireless_selected(target))return false;
    } else if(target!=M1_TRANSPORT_USB)return false;
    selecting=false;return true;
}
static bool pair_mode(void *context,m1_transport_t target)
{
    if(target==M1_TRANSPORT_USB)return false;
    if(!pairing) {
        if(!select_mode(context,target) || !m1_wireless_request_pair(true,now))return false;
        pairing=true;requested=target;
    }
    if(target!=requested || !m1_wireless_pair_complete())return false;
    pairing=false;return true;
}
const m1_transport_ops_t *m1_transport_ops(void)
{
    static const m1_transport_ops_t ops={drained,select_mode,NULL,available,pair_mode};
    return &ops;
}
