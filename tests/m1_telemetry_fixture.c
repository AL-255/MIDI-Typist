/* Offline fixture: real board/app/encoder/control service, fake USB callbacks.
 * stdout is one complete SNAPSHOT SysEx for the matching Python GUI decoder. */
#include "m1_board.h"
#include "keyboard_telemetry.h"
#include "midi_control.h"
#include "scan_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t now;
static bool locked;
static uint8_t wire[MT_SYSEX_MAX_WIRE];
static size_t used;
static uint32_t millis(void) { return now; }
static bool ready(void) { return true; }
static uint32_t lock(void) { assert(!locked); locked=true; return 17; }
static void unlock(uint32_t old) { assert(locked && old==17); locked=false; }
static bool write_events(const uint8_t *events,uint32_t size)
{
    assert(size%4==0);
    for(unsigned i=0;i<size;i+=4) {
        unsigned cin=events[i]&15u,n=cin==4?3:cin-4;
        assert(events[i]>>4==MT_SYSEX_CABLE && cin>=4 && cin<=7);
        assert(used+n<=sizeof(wire));
        memcpy(wire+used,events+i+1,n); used+=n;
    }
    return true;
}
static void receive(uint8_t kind,const char *text,uint32_t sequence)
{
    uint8_t encoded[MT_SYSEX_WIRE_SIZE(96)],events[4*MT_SYSEX_WIRE_SIZE(96)];
    size_t n=midi_sysex_encode(kind,123,sequence,(const uint8_t *)text,
                              text?strlen(text):0,encoded,sizeof(encoded));
    assert(n);
    size_t e=0;
    for(size_t i=0;i<n;) {
        size_t left=n-i,count=left<3?left:3;
        events[e++]=(MT_SYSEX_CABLE<<4)|(left<=3?4+count:4);
        for(unsigned j=0;j<3;++j)events[e++]=j<count?encoded[i++]:0;
    }
    midi_control_receive_usb(events,e);
    used=0;
    for(unsigned i=0;i<50;++i)midi_control_service();
    assert(used && wire[used-1]==0xf7);
}
static keyboard_app_t app;
static keyboard_raw_t raw;
static keyboard_midi_t midi;
static keyboard_menu_t menu;
static keyboard_calibration_t cal;
static keyboard_telemetry_status_t status={.storage_slot=255};
static bool command(const char *line)
{
    return keyboard_app_command(&app,line,now,true,&status.ack,&status.result);
}
int main(void)
{
    static const keyboard_app_ops_t ops={0};
    static const midi_control_port_t port={millis,ready,write_events,lock,unlock};
    assert(!midi_control_init(NULL));
    midi_control_service();
    assert(!midi_control_ready());
    keyboard_app_init(&app,&raw,&midi,&menu,&cal,&ops);
    uint8_t frame[SCAN_STREAM_GUI_SIZE];
    assert(keyboard_telemetry_encode(&app,&status,frame,sizeof(frame))==116);
    assert(frame[6]==0 && frame[7]==0 && frame[32]==0);
    uint16_t samples[82],lo[82],hi[82];
    for(unsigned i=0;i<82;++i) { samples[i]=3900+i; lo[i]=1000; hi[i]=4096; }
    keyboard_app_frame(&app,samples,82,1,lo,hi,true,now);
    scan_stream_init();
    assert(midi_control_init(&port));
    midi_control_command_handler(command);
    receive(MT_HELLO,NULL,0);
    midi_sysex_info_t info; uint8_t payload[MT_SYSEX_MAX_PAYLOAD];
    assert(midi_sysex_decode(wire,used,&info,payload,sizeof(payload)));
    assert(info.kind==MT_READY && info.session==123);
    assert(midi_control_ready());
    receive(MT_COMMAND,"cfg set 7 81 2500 2700",1);
    assert(status.ack==7 && status.result==1 && raw.press[81]==2500);
    receive(MT_COMMAND,"cfg midi 8 81 60",2);
    assert(status.ack==8 && status.result==1 && midi.mapping[81]==60);
    receive(MT_COMMAND,"cfg key 9 81 135",3);
    assert(status.ack==9 && status.result==1 && raw.keycode[81]==135);
    status.sequence=42; status.storage_generation=0x12345678;
    size_t size=keyboard_telemetry_encode(&app,&status,frame,sizeof(frame));
    assert(size==1508);
    assert(!keyboard_telemetry_encode(&app,&status,frame,size-1));
    scan_stream_last_key(3500,42,81);
    scan_stream_push(samples,82,1,0);
    used=0; assert(scan_stream_service());
    for(unsigned i=0;i<50;++i)midi_control_service();
    assert(midi_sysex_decode(wire,used,&info,payload,sizeof(payload)));
    assert(info.kind==MT_SAMPLES && info.length==20 && payload[14]==81);
    assert((payload[12]|(unsigned)payload[13]<<8)==3981);
    scan_stream_gui();
    assert(scan_stream_gui_push(frame,size));
    used=0; assert(scan_stream_service());
    for(unsigned i=0;i<100;++i)midi_control_service();
    assert(used && wire[used-1]==0xf7);
    assert(fwrite(wire,1,used,stdout)==used);
    return 0;
}
