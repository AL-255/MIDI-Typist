#include <assert.h>
#include <string.h>
#include "control_port.h"
#include "defaults.h"
#include "midi_control.h"
#include "scan_stream.h"

static uint32_t now, mask;
static bool connected, writable;
static unsigned commands;
static uint8_t output[8192];
static size_t output_size;
uint32_t control_port_millis(void) { return now; }
uint32_t control_port_lock(void) { uint32_t old=mask; mask=1; return old; }
void control_port_unlock(uint32_t state) { assert(mask==1); mask=state; }
bool control_port_ready(void) { return connected; }
bool control_port_write(const uint8_t *events,size_t length)
{
    if(!writable)return false;
    assert(length%4==0 && length<=4*MIDI_CONTROL_TX_EVENTS);
    for(size_t i=0;i<length;i+=4) {
        assert(events[i]>>4==MT_SYSEX_CABLE);
        unsigned cin=events[i]&15u, count=cin==4?3:cin-4;
        assert(cin>=4 && cin<=7 && output_size+count<=sizeof(output));
        memcpy(output+output_size,events+i+1,count); output_size+=count;
    }
    return true;
}
static bool command(const char *text)
{
    assert(!mask);
    ++commands;
    return !strcmp(text,"test");
}
static void send_on(uint8_t cable,uint8_t kind,uint32_t session,uint32_t seq,const char *text)
{
    uint8_t wire[MT_SYSEX_MAX_WIRE];
    size_t length=midi_sysex_encode(kind,session,seq,(const uint8_t *)text,
        text?strlen(text):0,wire,sizeof(wire));
    assert(length);
    for(size_t i=0;i<length;) {
        size_t count=length-i; if(count>3)count=3;
        uint8_t event[4]={(cable<<4)|(length-i>3?4:4+count),0,0,0};
        memcpy(event+1,wire+i,count);
        midi_control_receive_usb(event,sizeof(event)); i+=count;
    }
}
static void send(uint8_t kind,uint32_t session,uint32_t seq,const char *text)
{ send_on(MT_SYSEX_CABLE,kind,session,seq,text); }
static void drain(void) { for(unsigned i=0;i<100;++i)midi_control_service(); }
static size_t take(uint8_t kind,uint32_t session,uint32_t sequence,uint8_t *payload)
{
    midi_sysex_info_t info;
    assert(midi_sysex_decode(output,output_size,&info,payload,MT_SYSEX_MAX_PAYLOAD));
    assert(info.kind==kind && info.session==session && info.sequence==sequence);
    output_size=0;return info.length;
}
static void reset(void)
{
    connected=writable=true;now=mask=commands=0;output_size=0;
    midi_control_init();scan_stream_init();midi_control_command_handler(command);
}
static void hello(uint32_t session)
{
    uint8_t data[MT_SYSEX_MAX_PAYLOAD];
    send(MT_HELLO,session,0,NULL);drain();
    assert(take(MT_READY,session,0,data)>6 && !memcmp(data,"build=",6));
    assert(midi_control_ready());
}
static void protocol(void)
{
    uint8_t data[MT_SYSEX_MAX_PAYLOAD];reset();
    send_on(0,MT_HELLO,7,0,NULL);drain();assert(!midi_control_ready() && !output_size);
    hello(7);
    send(MT_COMMAND,8,1,"test");drain();assert(!commands && !output_size);
    send(MT_COMMAND,7,1,"test");assert(!commands);drain();
    assert(commands==1);assert(take(MT_ACK,7,1,data)==0);
    send(MT_COMMAND,7,1,"test");drain();take(MT_ERROR,7,1,data);assert(commands==1);
    send(MT_COMMAND,7,2,"git");drain();
    assert(take(MT_ACK,7,2,data)>4 && !memcmp(data,"git=",4));assert(commands==1);
    send(MT_COMMAND,7,3,"bad");drain();take(MT_ERROR,7,3,data);assert(commands==2);
    mask=1;midi_control_service();assert(mask==1);mask=0;
    now=MIDI_CONTROL_LEASE_MS;drain();assert(!midi_control_ready());
    hello(9);send(MT_CLOSE,9,0,NULL);drain();assert(!midi_control_ready());
}
static void backpressure_and_reset(void)
{
    uint8_t data[MT_SYSEX_MAX_PAYLOAD],expected[MT_SYSEX_MAX_PAYLOAD];
    reset();hello(1);memset(expected,0xa5,sizeof(expected));
    writable=false;assert(midi_control_publish(MT_SNAPSHOT,expected,sizeof(expected)));
    drain();assert(!output_size);assert(!midi_control_publish(MT_SNAPSHOT,expected,1));
    writable=true;drain();assert(take(MT_SNAPSHOT,1,0,data)==sizeof(expected));
    assert(!memcmp(data,expected,sizeof(data)));
    writable=false;scan_stream_gui();scan_stream_gui_push(expected);
    assert(scan_stream_service());
    midi_control_usb_reset();scan_stream_usb_reset();drain();scan_stream_service();
    assert(!midi_control_ready() && !scan_stream_active());
    writable=true;hello(2);drain();assert(!output_size);
}
static uint32_t le32(const uint8_t *p)
{ return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void capture(void)
{
    uint8_t data[MT_SYSEX_MAX_PAYLOAD];uint16_t samples[2]={4096,3000};
    reset();hello(1);scan_stream_last_key(3500,42,1);
    for(unsigned i=0;i<MIDI_CONTROL_DEVICE_RECORDS;++i)scan_stream_push(samples,2,4,i);
    scan_stream_push(samples,2,4,0);assert(scan_stream_dropped()==1);
    unsigned sequence=0;
    while(sequence<MIDI_CONTROL_DEVICE_RECORDS) {
        assert(scan_stream_service());drain();size_t n=take(MT_SAMPLES,1,0,data);
        assert(n%SCAN_STREAM_KEY_SIZE==0);
        for(size_t at=0;at<n;at+=SCAN_STREAM_KEY_SIZE) {
            assert(!memcmp(data+at,"HKL1",4) && le32(data+at+4)==42);
            assert(le32(data+at+8)==sequence++ && !(data[at+15]&6));
            assert(data[at+12]==(3000&255) && data[at+13]==(3000>>8));
        }
    }
    assert(scan_stream_service());drain();assert(take(MT_SAMPLES,1,0,data)==20);
    assert(data[15]&2);assert(!scan_stream_service());
    scan_stream_last_key(3500,43,1);scan_stream_push(samples,2,4,0);
    scan_stream_push(samples,2,1,0);assert(scan_stream_service());drain();
    assert(take(MT_SAMPLES,1,0,data)==40 && (data[35]&4));
}
int main(void) { protocol();backpressure_and_reset();capture();return 0; }
