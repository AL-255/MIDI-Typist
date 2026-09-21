#include "m1_wireless.h"
#include "defaults.h"

enum { NONE, MODE, QUERY, POLL, KEYS, BITMAP };
static bool active,faulted,confirmed,have_status,pending,mode_sent,query_sent,poll_sent,linked;
static unsigned flight,part;
static m1_transport_t target;
static m1_radio_status_t status;
static m1_radio_keyboard_t committed,staged;
static uint32_t now,started,last_mode,last_query,last_poll,last_report,status_at,errors,reports;

static void fail(void)
{
    ++errors;faulted=true;confirmed=false;pending=false;flight=NONE;
    m1_radio_stop();
}
bool m1_wireless_init(m1_transport_t mode,bool released,uint32_t tick)
{
    if(active || !released || !m1_transport_valid(mode) || mode==M1_TRANSPORT_USB ||
       !m1_radio_ready() || !m1_radio_healthy())return false;
    target=mode;now=started=tick;errors=reports=0;
    last_mode=last_query=last_poll=last_report=status_at=tick;
    confirmed=have_status=mode_sent=query_sent=poll_sent=faulted=linked=false;
    committed=(m1_radio_keyboard_t){0};staged=committed;
    status=(m1_radio_status_t){0};flight=NONE;part=KEYS;
    pending=true; /* mandatory neutral baseline before accepting presses */
    active=true;return true;
}
void m1_wireless_stop(void)
{
    if(active)m1_radio_stop();
    active=false;confirmed=false;pending=false;flight=NONE;
}
bool m1_wireless_healthy(void) { return active && !faulted && m1_radio_healthy(); }
bool m1_wireless_ready(void)
{
    return m1_wireless_healthy() && confirmed && have_status && status.state==M1_RADIO_STATE_REPORTS &&
        (uint32_t)(now-status_at)<M1_RADIO_STATUS_TIMEOUT_US;
}
bool m1_wireless_local_idle(void)
{ return m1_wireless_healthy() && !pending && flight==NONE && m1_radio_ready(); }
uint32_t m1_wireless_reports_sent(void) { return reports; }
uint32_t m1_wireless_errors(void) { return errors; }
bool m1_wireless_status(m1_radio_status_t *out)
{
    if(!out || !m1_wireless_healthy() || !have_status ||
       (uint32_t)(now-status_at)>=M1_RADIO_STATUS_TIMEOUT_US)return false;
    *out=status;return true;
}
bool m1_wireless_offer(const keyboard_report_t *report)
{
    if(!report || !m1_wireless_ready() || pending)return false;
    staged=committed;
    if(!m1_radio_keyboard_update(&staged,report))return false;
    pending=true;part=KEYS;return true;
}
static void receive(const uint8_t *bytes,size_t length)
{
    m1_radio_reply_t reply;m1_radio_status_t received;
    if(!m1_radio_decode(bytes,length,&reply)) { ++errors;return; }
    if(!m1_radio_status(&reply,&received))return;
    status=received;status_at=now;have_status=true;
    if(linked && (received.mode!=target || received.state!=M1_RADIO_STATE_REPORTS)) {
        /* A peer discontinuity cannot silently replay an old held report. */
        fail();return;
    }
    confirmed=mode_sent && received.mode==target;
    if(confirmed && received.state==M1_RADIO_STATE_REPORTS)linked=true;
}
static bool send(unsigned kind,const m1_radio_packet_t *packet)
{
    if(!m1_radio_exchange(packet,now))return false;
    flight=kind;return true;
}
void m1_wireless_service(uint32_t tick)
{
    if(!active || faulted)return;
    now=tick;m1_radio_service(now);
    if(!m1_radio_healthy()) { fail();return; }
    uint8_t rx[M1_RADIO_BUFFER_BYTES];size_t length;
    if(flight && m1_radio_take(rx,sizeof(rx),&length)) {
        unsigned done=flight;flight=NONE;
        /* The original parser is gated to explicit poll transactions. Ordinary
         * full-duplex command RX is not an unsolicited status/acknowledgement. */
        if(done==POLL)receive(rx,length);
        else if(done==KEYS)part=BITMAP;
        else if(done==BITMAP) { committed=staged;pending=false;++reports;last_report=now; }
    }
    if(faulted)return;
    if((!confirmed && (uint32_t)(now-started)>=M1_RADIO_MODE_TIMEOUT_US) ||
       (confirmed && (uint32_t)(now-status_at)>=M1_RADIO_STATUS_TIMEOUT_US)) {
        fail();return;
    }
    if(flight || !m1_radio_ready())return;
    m1_radio_packet_t packet;
    if(m1_radio_data_pending() && (!poll_sent || (uint32_t)(now-last_poll)>=M1_RADIO_POLL_US)) {
        m1_radio_make_poll(&packet);
        if(send(POLL,&packet)) { last_poll=now;poll_sent=true; }
        return;
    }
    if(!confirmed && (!mode_sent || (uint32_t)(now-last_mode)>=M1_RADIO_QUERY_US)) {
        uint8_t mode=target;
        (void)m1_radio_encode(&packet,M1_RADIO_MODE,&mode,1);
        if(send(MODE,&packet)) { mode_sent=true;last_mode=now; }
        return;
    }
    if(!query_sent || (uint32_t)(now-last_query)>=M1_RADIO_QUERY_US) {
        const uint8_t query=0; /* deterministic unused request byte, no vendor command */
        (void)m1_radio_encode(&packet,M1_RADIO_STATUS_REQUEST,&query,1);
        if(send(QUERY,&packet)) { query_sent=true;last_query=now; }
        return;
    }
    uint32_t interval=target==M1_TRANSPORT_RADIO?M1_RADIO_RF_REPORT_US:M1_RADIO_BT_REPORT_US;
    if(pending && m1_wireless_ready() &&
       (!reports || part==BITMAP || (uint32_t)(now-last_report)>=interval)) {
        (void)m1_radio_keyboard_packet(&staged,part==KEYS?M1_RADIO_KEY_LIST:M1_RADIO_KEY_BITMAP,&packet);
        (void)send(part,&packet);
    }
}
