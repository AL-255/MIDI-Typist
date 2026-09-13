#include "scan_stream.h"
#include "midi_control.h"
#include "defaults.h"
#include <string.h>

/* GUI is latest-only; capture is loss-detecting and fail-stop. Both use the
 * SysEx control cable. Publication copies the entire payload before return. */
#define RECORDS MIDI_CONTROL_DEVICE_RECORDS
#define BATCH MIDI_CONTROL_SAMPLE_BATCH
_Static_assert(BATCH * SCAN_STREAM_KEY_SIZE <= MT_SYSEX_MAX_PAYLOAD, "capture batch fits SysEx");
_Static_assert(RECORDS * SCAN_STREAM_KEY_SIZE >= SCAN_STREAM_GUI_SIZE, "shared buffer fits snapshot");
enum { OFF, GUI, KEY, DUMP };
static uint8_t s_records[RECORDS*SCAN_STREAM_KEY_SIZE];
static uint8_t s_packet[SCAN_STREAM_GUI_SIZE];
static unsigned s_head,s_tail,s_count,s_mode;
static uint32_t s_sequence,s_dropped,s_session;
static bool s_enabled,s_first,s_fault,s_fault_sent;
static volatile bool s_reset;
static uint8_t s_sensor,s_profile;
static uint16_t s_threshold;
static void le16(uint8_t *p,uint16_t v) { p[0]=v;p[1]=v>>8; }
static void le32(uint8_t *p,uint32_t v) { le16(p,v);le16(p+2,v>>16); }
void scan_stream_init(void)
{
    s_head=s_tail=s_count=s_mode=0;s_sequence=s_dropped=s_session=0;
    s_enabled=s_first=s_fault=s_fault_sent=s_reset=false;s_sensor=255;s_profile=0;
}
void scan_stream_stop(void) { s_enabled=false;s_mode=OFF;s_dropped+=s_count;s_head=s_tail=s_count=0; }
void scan_stream_start(void) { s_enabled=s_mode!=OFF && midi_control_ready(); }
void scan_stream_gui(void) { scan_stream_stop();s_mode=GUI;scan_stream_start(); }
bool scan_stream_gui_enabled(void) { return s_mode==GUI && s_enabled; }
void scan_stream_gui_push(const uint8_t report[SCAN_STREAM_GUI_SIZE])
{
    if(!scan_stream_gui_enabled() || !midi_control_ready())return;
    memcpy(s_records,report,SCAN_STREAM_GUI_SIZE);s_count=1;
}
void scan_stream_last_key(uint16_t threshold,uint32_t session,uint8_t sensor)
{
    scan_stream_stop();s_mode=KEY;s_first=true;s_fault=s_fault_sent=false;
    s_sequence=0;s_session=session;s_sensor=sensor;s_threshold=threshold;s_profile=0;
    scan_stream_start();
}
bool scan_stream_dump_ready(void)
{
    if(!midi_control_ready() || (s_mode==DUMP && s_count))return false;
    scan_stream_stop();s_mode=DUMP;scan_stream_start();return true;
}
void scan_stream_dump_push(const uint8_t report[128]) { if(s_mode==DUMP && !s_count) { memcpy(s_records,report,128);s_count=1; } }
bool scan_stream_active(void) { return s_enabled; }
bool scan_stream_enabled(void) { return s_enabled; }
uint32_t scan_stream_dropped(void) { return s_dropped; }
void scan_stream_usb_reset(void) { s_reset=true; }
static void key_record(uint8_t *out,uint16_t value,uint8_t flags)
{
    memcpy(out,"HKL1",4);le32(out+4,s_session);le32(out+8,s_sequence++);
    le16(out+12,value);out[14]=s_sensor;out[15]=flags|(s_first?1u:0u);s_first=false;
    le16(out+16,s_threshold);
    unsigned checksum=0;for(unsigned i=0;i<18;i+=2)checksum+=out[i]|(unsigned)out[i+1]<<8;
    le16(out+18,checksum);
}
void scan_stream_push(const uint16_t *samples,uint8_t count,uint8_t profile,uint32_t tick)
{
    (void)tick;
    if(!s_enabled || s_mode!=KEY || s_fault)return;
    if(s_reset || !midi_control_ready() || s_count==RECORDS) { ++s_dropped;s_fault=true;return; }
    uint8_t flags=0;
    if(s_sensor>=count || (s_profile && s_profile!=profile))flags=4;
    for(unsigned i=0;i<count;++i)if(!samples[i] || samples[i]>4096)flags=4;
    s_profile=profile;
    key_record(s_records+s_head*SCAN_STREAM_KEY_SIZE,s_sensor<count?samples[s_sensor]:0,flags);
    s_head=(s_head+1u)%RECORDS;++s_count;
    if(flags)s_fault=true;
}
bool scan_stream_service(void)
{
    if(s_reset || !midi_control_ready()) { s_reset=false;scan_stream_stop();return false; }
    if(!s_enabled)return false;
    if(s_mode==KEY && s_fault && !s_fault_sent && !s_count) {
        key_record(s_records,0,2);s_head=1;s_tail=0;s_count=1;s_fault_sent=true;
    }
    if(!s_count)return false;
    unsigned count=s_mode==KEY?(s_count<BATCH?s_count:BATCH):1;
    unsigned size=s_mode==KEY?SCAN_STREAM_KEY_SIZE:s_mode==GUI?SCAN_STREAM_GUI_SIZE:128u;
    for(unsigned i=0;i<count;++i)
        memcpy(s_packet+i*size,s_records+(s_mode==KEY?(s_tail+i)%RECORDS:0)*size,size);
    if(!midi_control_publish(s_mode==KEY?MT_SAMPLES:s_mode==GUI?MT_SNAPSHOT:MT_DUMP,s_packet,count*size))return false;
    s_tail=(s_tail+count)%RECORDS;s_count-=count;return true;
}
