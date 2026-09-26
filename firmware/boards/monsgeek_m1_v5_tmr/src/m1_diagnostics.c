#include "m1_diagnostics.h"
#include "m1_boot.h"
#include "m1_image.h"
#include "m1_storage.h"
#include "m1_factory.h"
#include "m1_hal.h"
#include "m1_wireless.h"
#include "m1_usb.h"
#include "m1_usb_hal.h"
#include "midi_control.h"
#include "scan_stream.h"
#include <string.h>

static bool initialized,reported,update_requested;
static bool runtime_fault,neutral_sent,consumer_neutral;
static uint32_t runtime_detail;
static unsigned cleanup_event;
static uint32_t now,epoch;
static uint32_t lock(void) { uint32_t mask=__get_PRIMASK();__disable_irq();return mask; }
static void unlock(uint32_t mask) { __set_PRIMASK(mask); }
static uint32_t millis(void) { return now; }
static bool ready(void) { return epoch==m1_usb_generation() && m1_usb_ready(); }
static bool send(const uint8_t *events,uint32_t size)
{
    uint32_t mask=lock();bool accepted=ready() && m1_usb_midi_send(events,size);
    unlock(mask);return accepted;
}
static bool command(const char *line)
{
    if(!strcmp(line,"runtime stats"))return runtime_fault && m1_live_publish_stats();
    if(!strcmp(line,"boot scan")) {
        uint16_t samples[M1_KEY_COUNT];uint32_t sequence;
        if(!m1_boot_scan(samples,&sequence))return false;
        uint8_t payload[12u+2u*M1_KEY_COUNT]={'M','1','B','S',1,0,M1_KEY_COUNT,0};
        for(unsigned i=0;i<4;++i)payload[8+i]=(uint8_t)(sequence>>(8*i));
        for(unsigned i=0;i<M1_KEY_COUNT;++i) {
            payload[12+2*i]=(uint8_t)samples[i];payload[13+2*i]=(uint8_t)(samples[i]>>8);
        }
        return midi_control_publish(MT_DUMP,payload,sizeof(payload));
    }
    if(!strcmp(line,"factory read")) {
        /* Fixed calibration fields only: no arbitrary address, serial data,
         * bootloader code, unlock or erase operation is exposed. */
        if(m1_boot_state()!=M1_BOOT_FAILED && !runtime_fault)return false;
        uint8_t payload[M1_FACTORY_DUMP_BYTES];
        if(m1_factory_read_dump(payload)!=M1_FACTORY_OK)return false;
        return midi_control_publish(MT_DUMP,payload,sizeof(payload));
    }
    if(!strcmp(line,"bootloader")) {
        if(m1_storage_check_recovery(true)!=M1_STORAGE_OK)return false;
        update_requested=true;return true;
    }
    if(!strcmp(line,"boot status") || !strcmp(line,"stream gui") ||
       !strncmp(line,"cfg get ",8)) { reported=false;return true; }
    return !strcmp(line,"stream off");
}
static size_t failure_text(char *text)
{
    if(runtime_fault) {
        strcpy(text,"Runtime failed: detail=0x");size_t length=strlen(text);
        for(unsigned i=0;i<8;++i)text[length++]="0123456789abcdef"[(runtime_detail>>(28u-4u*i))&15u];
        text[length]=0;strcat(text," store=0x");length=strlen(text);
        uint32_t value=m1_live_storage_error();
        for(unsigned i=0;i<8;++i)text[length++]="0123456789abcdef"[(value>>(28u-4u*i))&15u];
        text[length]=0;strcat(text," scan=0x");length=strlen(text);value=m1_hal_fault_reason();
        for(unsigned i=0;i<8;++i)text[length++]="0123456789abcdef"[(value>>(28u-4u*i))&15u];
        text[length]=0;strcat(text," radio=0x");length=strlen(text);
        value=m1_wireless_fault_detail();
        for(unsigned i=0;i<8;++i)text[length++]="0123456789abcdef"[(value>>(28u-4u*i))&15u];
        text[length]=0;return length;
    }
    static const char *const errors[]={"none","timebase","startup","power-source",
        "scan-pause","USB","radio-init","radio-link","scan-resume","application"};
    unsigned error=m1_boot_error();
    const char *name=error<sizeof(errors)/sizeof(errors[0])?errors[error]:"unknown";
    strcpy(text,"Boot failed: ");strcat(text,name);strcat(text," factory=0x");
    size_t length=strlen(text);uint32_t factory=m1_live_factory_result();
    for(unsigned i=0;i<8;++i)text[length++]= "0123456789abcdef"[(factory>>(28u-4u*i))&15u];
    text[length]=0;strcat(text," dma=0x");length=strlen(text);
    uint32_t counts=m1_hal_pretrigger_counts();
    for(unsigned i=0;i<8;++i)text[length++]="0123456789abcdef"[(counts>>(28u-4u*i))&15u];
    text[length]=0;return length;
}
void m1_diagnostics_runtime_fault(uint32_t detail)
{
    runtime_fault=true;runtime_detail=detail;
    initialized=reported=update_requested=neutral_sent=consumer_neutral=false;cleanup_event=0;
}
bool m1_diagnostics_service(uint32_t now_ms)
{
    if((m1_boot_state()==M1_BOOT_READY && !runtime_fault) || !m1_usb_hw_running())return false;
    now=now_ms;
    if(!initialized) {
        static const midi_control_port_t port={millis,ready,send,lock,unlock};
        scan_stream_init();
        if(!midi_control_init(&port))return false;
        midi_control_command_handler(command);
        epoch=m1_usb_generation();initialized=true;
    }
    uint8_t events[M1_USB_HS_PACKET];uint32_t mask=lock();
    if(epoch!=m1_usb_generation()) {
        epoch=m1_usb_generation();midi_control_usb_reset();reported=false;update_requested=false;
        neutral_sent=consumer_neutral=false;cleanup_event=0;
    }
    unsigned size=m1_usb_midi_take(events,sizeof(events));
    if(size && ready())midi_control_receive_usb(events,size);
    unlock(mask);
    if(runtime_fault) {
        const keyboard_report_t neutral={0};
        if(!neutral_sent)neutral_sent=m1_usb_hid_send(&neutral);
        /* USB-MIDI performance cable: sustain off, all sound off, all notes
         * off on each of the 16 channels. Retry only busy endpoint offers. */
        static const uint8_t controllers[]={64,120,123};
        if(cleanup_event<16u*sizeof(controllers)) {
            uint8_t event[]={0x0b,0xb0u+cleanup_event/sizeof(controllers),
                controllers[cleanup_event%sizeof(controllers)],0};
            if(send(event,sizeof(event)))++cleanup_event;
        }
    }
    if(runtime_fault && ready() && !consumer_neutral)consumer_neutral=m1_usb_consumer_send(0);
    midi_control_service();
    if(!midi_control_ready())reported=false;
    if((m1_boot_state()==M1_BOOT_FAILED || runtime_fault) && !reported) {
        char text[112];size_t length=failure_text(text);
        reported=midi_control_publish(MT_LOG,(const uint8_t *)text,length);
    }
    return update_requested;
}
