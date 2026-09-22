#include "m1_wireless.h"
#include "defaults.h"
#include <string.h>

enum { NONE, MODE, QUERY, POLL, KEYS, BITMAP, BATTERY, SLEEP, CONSUMER, PAIR };
enum { PAIR_NONE,PAIR_QUEUED,PAIR_REFRESH };
static unsigned pair_state;
static bool pair_complete;
static uint32_t pair_at;
static bool active,faulted,confirmed,have_status,pending,mode_sent,query_sent,poll_sent,linked;
static unsigned flight,part;
static m1_transport_t target;
static m1_radio_status_t status;
static m1_radio_keyboard_t committed,staged;
static uint32_t now,started,last_mode,last_query,last_poll,last_report,status_at,errors,reports;
static uint8_t battery_value,battery_flight,battery_last,sleep_command,previous_sleep;
static bool battery_known,battery_sent,sleep_complete;
static uint32_t sleep_at;
static uint16_t consumer_committed,consumer_staged;
static bool consumer_pending,consumer_seen;
static uint32_t consumer_at;

static bool battery_pending(void)
{ return battery_known && (!battery_sent || battery_value!=battery_last); }
static bool neutral(const m1_radio_keyboard_t *keys)
{
    if(keys->modifiers || keys->rollover)return false;
    for(unsigned i=0;i<M1_RADIO_KEY_SLOTS;++i)if(keys->slots[i])return false;
    for(unsigned i=0;i<M1_RADIO_KEY_BITMAP_BYTES;++i)if(keys->bitmap[i])return false;
    return true;
}

static void fail(void)
{
    ++errors;faulted=true;confirmed=false;pending=false;flight=NONE;
    m1_radio_stop();
}
static void begin_session(m1_transport_t mode,uint32_t tick)
{
    target=mode;now=started=tick;errors=reports=0;
    last_mode=last_query=last_poll=last_report=status_at=tick;
    confirmed=have_status=mode_sent=query_sent=poll_sent=faulted=linked=false;
    battery_known=battery_sent=sleep_complete=false;
    battery_value=battery_flight=battery_last=sleep_command=previous_sleep=0;sleep_at=tick;
    committed=(m1_radio_keyboard_t){0};staged=committed;
    status=(m1_radio_status_t){0};flight=NONE;part=KEYS;
    pending=true; /* mandatory neutral baseline before accepting presses */
    consumer_committed=consumer_staged=0;consumer_pending=consumer_seen=false;consumer_at=tick;
    active=true;
    pair_state=PAIR_NONE;pair_complete=false;
}
bool m1_wireless_init(m1_transport_t mode,bool released,uint32_t tick)
{
    if(active || !released || !m1_transport_valid(mode) || mode==M1_TRANSPORT_USB ||
       !m1_radio_ready() || !m1_radio_healthy())return false;
    begin_session(mode,tick);return true;
}
bool m1_wireless_resume_retained(bool restored,uint32_t tick)
{
    if(!restored || sleep_command!=M1_RADIO_BT_RETAIN || !sleep_complete ||
       !m1_wireless_local_idle())return false;
    begin_session(target,tick);return true;
}
void m1_wireless_stop(void)
{
    if(active)m1_radio_stop();
    active=false;confirmed=false;pending=false;flight=NONE;
    consumer_pending=false;
    pair_state=PAIR_NONE;pair_complete=false;
}
bool m1_wireless_healthy(void) { return active && !faulted && m1_radio_healthy(); }
bool m1_wireless_mode(m1_transport_t *mode)
{ if(!mode || !m1_wireless_healthy())return false;*mode=target;return true; }
bool m1_wireless_ready(void)
{
    return target!=M1_TRANSPORT_USB && m1_wireless_healthy() && !sleep_command && !pair_state && confirmed && have_status && status.state==M1_RADIO_STATE_REPORTS &&
        (uint32_t)(now-status_at)<M1_RADIO_STATUS_TIMEOUT_US;
}
bool m1_wireless_selected(m1_transport_t mode)
{ return m1_wireless_healthy() && !sleep_command && target==mode && confirmed && have_status &&
    (uint32_t)(now-status_at)<M1_RADIO_STATUS_TIMEOUT_US; }
bool m1_wireless_switch_ready(void)
{ return m1_wireless_healthy() && !sleep_command && !pair_state && flight==NONE && m1_radio_ready() &&
    neutral(&committed) && (!pending || neutral(&staged)) &&
    !consumer_committed && (!consumer_pending || !consumer_staged); }
bool m1_wireless_select(m1_transport_t mode,uint32_t tick)
{
    if(!m1_transport_valid(mode) || !m1_wireless_switch_ready())return false;
    if(mode==target)return true;
    target=mode;now=started=status_at=tick;
    confirmed=have_status=mode_sent=query_sent=poll_sent=linked=false;
    battery_known=battery_sent=false;
    reports=0;pending=mode!=M1_TRANSPORT_USB;part=KEYS;
    pair_complete=false;
    committed=(m1_radio_keyboard_t){0};staged=committed;
    consumer_committed=consumer_staged=0;consumer_pending=consumer_seen=false;consumer_at=tick;
    return true;
}
bool m1_wireless_request_pair(bool released,uint32_t tick)
{
    if(!released || target==M1_TRANSPORT_USB || !m1_wireless_selected(target) ||
       !m1_wireless_switch_ready() || m1_radio_data_pending())return false;
    pair_state=PAIR_QUEUED;pair_complete=false;pair_at=tick;
    /* Only explicit neutral pairing may discard old link eligibility. A
     * spontaneous discontinuity still follows the normal fault policy. */
    linked=confirmed=have_status=false;
    battery_known=battery_sent=false;
    pending=false;return true;
}
bool m1_wireless_pair_complete(void)
{ return pair_complete && m1_wireless_selected(target); }
bool m1_wireless_pairing(void)
{ return m1_wireless_healthy() && target!=M1_TRANSPORT_USB &&
    (pair_state || (confirmed && have_status && status.state==M1_RADIO_STATE_PAIRING)); }
bool m1_wireless_local_idle(void)
{ return m1_wireless_healthy() && !pair_state && !pending && !consumer_pending && !battery_pending() &&
    (!sleep_command || sleep_complete) && flight==NONE && m1_radio_ready(); }
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
    const keyboard_report_t empty={0};
    /* Before a peer is report-eligible, its mandatory neutral baseline may
     * satisfy another neutral offer. This is queue ownership, not delivery;
     * non-neutral offers remain rejected and cannot be replayed on connect. */
    if(report && m1_wireless_healthy() && target!=M1_TRANSPORT_USB && !sleep_command &&
       !m1_wireless_ready() && pending && neutral(&committed) && neutral(&staged) &&
       !memcmp(report,&empty,sizeof(empty)))return true;
    if(!report || !m1_wireless_ready() || pending)return false;
    staged=committed;
    if(!m1_radio_keyboard_update(&staged,report))return false;
    pending=true;part=KEYS;return true;
}
bool m1_wireless_battery(const m1_battery_t *battery)
{
    if(!m1_wireless_healthy() || sleep_command || target==M1_TRANSPORT_USB)return false;
    if(!battery || !battery->valid || !battery->source_known ||
       !battery->percent || battery->percent>100u) {
        battery_known=battery_sent=false;return false;
    }
    battery_value=battery->percent;battery_known=true;return true;
}
bool m1_wireless_consumer(uint16_t usage)
{
    if(!m1_wireless_healthy() || target==M1_TRANSPORT_USB || sleep_command || consumer_pending)return false;
    /* No key action can be queued offline. Coalesce only the neutral baseline;
     * the live owner's readiness edge requests it again when a host appears. */
    if(!m1_wireless_ready())return !usage && !consumer_committed;
    if(consumer_seen && consumer_committed==usage)return true;
    consumer_staged=usage;consumer_pending=true;return true;
}
bool m1_wireless_battery_sent(uint8_t *percent)
{
    if(!percent || !m1_wireless_healthy() || !battery_known || !battery_sent)return false;
    *percent=battery_last;return true;
}
bool m1_wireless_request_sleep(uint8_t command,bool host_released)
{
    bool deepen=sleep_command==M1_RADIO_BT_RETAIN && sleep_complete && command==M1_RADIO_SLEEP;
    if(!host_released || !m1_wireless_healthy() || pair_state || (sleep_command && !deepen) || flight ||
       !m1_radio_ready() || !neutral(&committed) || consumer_committed || consumer_pending ||
       (pending && (reports || !neutral(&staged))) ||
       (command!=M1_RADIO_SLEEP && command!=M1_RADIO_BT_RETAIN))return false;
    if(command==M1_RADIO_BT_RETAIN && (target==M1_TRANSPORT_RADIO || !m1_wireless_ready()))return false;
    previous_sleep=deepen?sleep_command:0;
    sleep_command=command;sleep_at=now;sleep_complete=false;pending=false;
    battery_known=battery_sent=false;return true;
}
bool m1_wireless_cancel_sleep(void)
{
    if(!m1_wireless_healthy() || !sleep_command || sleep_complete || flight==SLEEP)return false;
    sleep_command=previous_sleep;sleep_complete=previous_sleep!=0;previous_sleep=0;
    if(sleep_complete)return true; /* retain quiet BT state, never fake a wake */
    if(!reports) { pending=true;part=KEYS; } /* restore unsent startup baseline */
    return true;
}
uint8_t m1_wireless_sleep_sent(void)
{ return m1_wireless_healthy() && sleep_complete?sleep_command:0; }
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
    if(confirmed && pair_state==PAIR_REFRESH) {
        pair_state=PAIR_NONE;pair_complete=true;
        committed=(m1_radio_keyboard_t){0};staged=committed;
        reports=0;pending=true;part=KEYS; /* fresh neutral before any press */
    }
    if(confirmed && target!=M1_TRANSPORT_USB && received.state==M1_RADIO_STATE_REPORTS)linked=true;
}
static bool send(unsigned kind,const m1_radio_packet_t *packet)
{
    if(!m1_radio_exchange(packet,now))return false;
    flight=kind;return true;
}
void m1_wireless_service(uint32_t tick)
{
    if(!active || faulted)return;
    now=tick;
    if(pair_state && (uint32_t)(now-pair_at)>=M1_RADIO_MODE_TIMEOUT_US) { fail();return; }
    /* The request deadline includes queueing AND observed completion. A late
     * DMA flag must not retroactively authorize an expired power handoff. */
    if(sleep_command && !sleep_complete && (uint32_t)(now-sleep_at)>=M1_RADIO_SLEEP_TIMEOUT_US) {
        fail();return;
    }
    m1_radio_service(now);
    if(!m1_radio_healthy()) { fail();return; }
    uint8_t rx[M1_RADIO_BUFFER_BYTES];size_t length;
    if(flight && m1_radio_take(rx,sizeof(rx),&length)) {
        unsigned done=flight;flight=NONE;
        /* The original parser is gated to explicit poll transactions. Ordinary
         * full-duplex command RX is not an unsolicited status/acknowledgement. */
        if(done==POLL)receive(rx,length);
        else if(done==KEYS)part=BITMAP;
        else if(done==BITMAP) { committed=staged;pending=false;++reports;last_report=now; }
        else if(done==BATTERY) { battery_last=battery_flight;battery_sent=battery_known; }
        else if(done==CONSUMER) {
            consumer_committed=consumer_staged;consumer_pending=false;consumer_seen=true;consumer_at=now;
        }
        else if(done==SLEEP)sleep_complete=true;
        else if(done==PAIR) {
            pair_state=PAIR_REFRESH;started=now;
            confirmed=have_status=false;query_sent=false;
        }
    }
    if(faulted)return;
    if(pair_state==PAIR_QUEUED) {
        if(!flight && m1_radio_ready()) {
            m1_radio_packet_t packet;
            if(!m1_radio_make_pair(&packet,target)) { fail();return; }
            (void)send(PAIR,&packet);
        }
        return;
    }
    /* Once the control packet was accepted, neither a later status timeout
     * nor a request cancellation may resume ordinary traffic into a sleeping
     * peer. The outer power/wake coordinator owns all subsequent decisions. */
    if(sleep_command) {
        if(sleep_complete)return;
        if(!flight && m1_radio_ready()) {
            m1_radio_packet_t packet;
            (void)m1_radio_encode(&packet,M1_RADIO_CONTROL,&sleep_command,1);
            (void)send(SLEEP,&packet);
        }
        return;
    }
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
    if(!confirmed && pair_state!=PAIR_REFRESH && (!mode_sent || (uint32_t)(now-last_mode)>=M1_RADIO_QUERY_US)) {
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
    /* Battery is latest-only metadata. Never interrupt an accepted keyboard
     * pair, and never starve releases with repeated percentage changes. */
    if(!pending && !consumer_pending && battery_pending() && m1_wireless_ready()) {
        (void)m1_radio_encode(&packet,M1_RADIO_BATTERY,&battery_value,1);
        if(send(BATTERY,&packet))battery_flight=battery_value;
        return;
    }
    if(pending && m1_wireless_ready() &&
       (!reports || part==BITMAP || (uint32_t)(now-last_report)>=interval)) {
        (void)m1_radio_keyboard_packet(&staged,part==KEYS?M1_RADIO_KEY_LIST:M1_RADIO_KEY_BITMAP,&packet);
        (void)send(part,&packet);
        return;
    }
    if(consumer_pending && m1_wireless_ready() && (!pending || part==KEYS) &&
       (!consumer_seen || (uint32_t)(now-consumer_at)>=interval)) {
        uint8_t payload[3]={3,(uint8_t)consumer_staged,(uint8_t)(consumer_staged>>8)};
        (void)m1_radio_encode(&packet,M1_RADIO_REPORT,payload,sizeof(payload));
        (void)send(CONSUMER,&packet);
    }
}
