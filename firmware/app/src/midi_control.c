#include "midi_control.h"
#include "defaults.h"
#include "keyboard_build.h"
#include "control_port.h"
#include <string.h>
#include "scan_stream.h"

/* ISR owns only framing. CRC, commands and flash operations run in main.
 * One complete command mailbox: host must wait for ACK before another command.
 * TX holds one immutable SysEx; control_port_write copies each submitted
 * chunk into its own DMA buffer. Cable-0 notes get first use of the endpoint. */
static uint8_t s_rx[MT_SYSEX_WIRE_SIZE(MIDI_CONTROL_COMMAND_MAX)];
static volatile size_t s_rx_used,s_rx_ready;
static volatile bool s_receiving,s_reset,s_active;
static volatile uint32_t s_rx_at;
static uint8_t s_tx[MT_SYSEX_MAX_WIRE];
static size_t s_tx_size,s_tx_at;
static uint32_t s_session,s_sequence,s_activity;
static bool (*s_command)(const char *);
static uint8_t s_reply_kind,s_reply[128];
_Static_assert(sizeof("build=" MT_BUILD_INFO)-1 <= sizeof(s_reply), "build identity exceeds READY payload");
static size_t s_reply_size;
static uint32_t s_reply_sequence;

static void stop_streams(void)
{
    scan_stream_stop();
}
void midi_control_init(void)
{
    s_rx_used=s_rx_ready=s_tx_size=s_tx_at=0;
    s_receiving=s_reset=s_active=false;s_session=s_sequence=s_activity=0;
    s_reply_kind=0;s_command=NULL;
}
void midi_control_command_handler(bool (*handler)(const char *)) { s_command=handler; }
void midi_control_usb_reset(void)
{
    s_active=false;s_reset=true;s_receiving=false;s_rx_used=s_rx_ready=0;
}
bool midi_control_ready(void) { return s_active && control_port_ready(); }

void midi_control_receive_usb(const uint8_t *events,size_t length)
{
    if(!events || length%4) { s_receiving=false;s_rx_used=0;return; }
    for(size_t at=0;at<length;at+=4) {
        const uint8_t *event=events+at;
        if(event[0]>>4!=MT_SYSEX_CABLE) continue;
        unsigned cin=event[0]&15u;
        if(cin==15u && event[1]>=0xf8u) continue; /* real-time may interleave */
        if(cin<4u || cin>7u || s_rx_ready) { s_receiving=false;s_rx_used=0;continue; }
        unsigned count=cin==4u?3u:cin-4u;
        if(cin!=4u && event[count]!=0xf7u) { s_receiving=false;s_rx_used=0;continue; }
        for(unsigned i=1;i<=count;++i) {
            uint8_t byte=event[i];
            if(byte==0xf0u) { s_receiving=true;s_rx_used=0;s_rx_at=control_port_millis(); }
            if(!s_receiving) continue;
            if((byte&128u) && byte!=0xf0u && byte!=0xf7u) { s_receiving=false;s_rx_used=0;break; }
            if(s_rx_used==sizeof(s_rx)) { s_receiving=false;s_rx_used=0;break; }
            s_rx[s_rx_used++]=byte;
            if(byte==0xf7u) {
                if(cin!=4u && i==count) s_rx_ready=s_rx_used;
                s_receiving=false;s_rx_used=0;break;
            }
        }
    }
}
static void reply(uint8_t kind,uint32_t sequence,const char *message)
{
    s_reply_kind=kind;s_reply_sequence=sequence;s_reply_size=strlen(message);
    if(s_reply_size>sizeof(s_reply))s_reply_size=sizeof(s_reply);
    memcpy(s_reply,message,s_reply_size);
}
static void receive_command(uint32_t now)
{
    uint8_t wire[sizeof(s_rx)],payload[MIDI_CONTROL_COMMAND_MAX+1u];size_t size;
    uint32_t irq=control_port_lock();
    size=s_rx_ready;
    if(size) { memcpy(wire,s_rx,size);s_rx_ready=0; }
    if(s_receiving && (uint32_t)(now-s_rx_at)>=MIDI_CONTROL_RX_TIMEOUT_MS) {
        s_receiving=false;s_rx_used=0;
    }
    control_port_unlock(irq);
    midi_sysex_info_t info;
    if(!size || !midi_sysex_decode(wire,size,&info,payload,MIDI_CONTROL_COMMAND_MAX))return;
    if(info.kind==MT_HELLO && !info.sequence && !info.length) {
        stop_streams();s_session=info.session;s_sequence=0;s_active=true;s_activity=now;
        reply(MT_READY,0,"build=" MT_BUILD_INFO);return;
    }
    if(!s_active || info.session!=s_session)return;
    if(info.kind==MT_KEEPALIVE && !info.sequence && !info.length) { s_activity=now;return; }
    if(info.kind==MT_CLOSE && !info.sequence && !info.length) { s_active=false;stop_streams();return; }
    if(info.kind!=MT_COMMAND || !info.sequence || info.sequence!=s_sequence+1u || s_reply_kind) {
        reply(MT_ERROR,info.sequence,"command order/busy");return;
    }
    s_activity=now;s_sequence=info.sequence;
    if(!info.length) { reply(MT_ERROR,info.sequence,"empty command");return; }
    for(unsigned i=0;i<info.length;++i) if(payload[i]<32u || payload[i]>126u) {
        reply(MT_ERROR,info.sequence,"command must be one printable ASCII message");return;
    }
    payload[info.length]=0;
    if(!strcmp((const char *)payload,"git")) {
        reply(MT_ACK,info.sequence,MT_GIT_REPLY);return;
    }
    bool ok=s_command && s_command((const char *)payload);
    reply(ok?MT_ACK:MT_ERROR,info.sequence,ok?"":"unsupported command");
}
bool midi_control_publish(uint8_t kind,const uint8_t *data,size_t length)
{
    if(!midi_control_ready() || s_reply_kind || s_tx_at<s_tx_size)return false;
    s_tx_size=midi_sysex_encode(kind,s_session,0,data,length,s_tx,sizeof(s_tx));s_tx_at=0;
    return s_tx_size!=0;
}
void midi_control_service(void)
{
    if(s_reset) { s_reset=false;s_active=false;s_tx_size=s_tx_at=0;s_reply_kind=0;stop_streams(); }
    uint32_t now=control_port_millis();
    if(s_active && (uint32_t)(now-s_activity)>=MIDI_CONTROL_LEASE_MS) { s_active=false;stop_streams(); }
    receive_command(now);
    if(!midi_control_ready()) { s_tx_size=s_tx_at=0;s_reply_kind=0;return; }
    if(s_reply_kind && s_tx_at==s_tx_size) {
        s_tx_size=midi_sysex_encode(s_reply_kind,s_session,s_reply_sequence,s_reply,s_reply_size,s_tx,sizeof(s_tx));
        s_tx_at=0;s_reply_kind=0;
    }
    if(s_tx_at==s_tx_size)return;
    uint8_t events[4u*MIDI_CONTROL_TX_EVENTS];size_t used=0,position=s_tx_at;
    /* 16 events per pass: bounded CPU and an endpoint opportunity for notes
     * before the next chunk, at either USB speed. */
    while(position<s_tx_size && used<sizeof(events)) {
        size_t remain=s_tx_size-position,count=remain<3?remain:3;
        events[used++]=(MT_SYSEX_CABLE<<4)|(remain<=3?4u+count:4u);
        for(unsigned i=0;i<3;++i)events[used++]=i<count?s_tx[position++]:0;
    }
    if(control_port_write(events,used))s_tx_at=position;
}
