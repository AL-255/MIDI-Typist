#include "m1_diagnostics.h"
#include "m1_boot.h"
#include "m1_image.h"
#include "m1_factory.h"
#include "m1_hal.h"
#include "m1_usb.h"
#include "m1_usb_hal.h"
#include "midi_control.h"
#include "scan_stream.h"
#include <string.h>

static bool initialized,reported,update_requested;
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
        if(m1_boot_state()!=M1_BOOT_FAILED)return false;
        m1_factory_record_t upper,lower;
        if(m1_factory_read(&upper,&lower)!=M1_FACTORY_OK)return false;
        uint8_t payload[8u+2u*sizeof(m1_factory_record_t)]={'M','1','F','C',1,0,
            M1_FACTORY_CELL_COUNT&255u,M1_FACTORY_CELL_COUNT>>8};
        _Static_assert(sizeof(m1_factory_record_t)==M1_FACTORY_VALUES_BYTES+3u,"packed calibration fields");
        memcpy(payload+8,&upper,sizeof(upper));
        memcpy(payload+8+sizeof(upper),&lower,sizeof(lower));
        return midi_control_publish(MT_DUMP,payload,sizeof(payload));
    }
    if(!strcmp(line,"bootloader")) {
        if(*(const volatile uint32_t *)M1_RECOVERY_FLAG_ADDRESS!=M1_RECOVERY_FLAG_VALUE)return false;
        update_requested=true;return true;
    }
    if(!strcmp(line,"boot status") || !strcmp(line,"stream gui") ||
       !strncmp(line,"cfg get ",8)) { reported=false;return true; }
    return !strcmp(line,"stream off");
}
static size_t failure_text(char *text)
{
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
bool m1_diagnostics_service(uint32_t now_ms)
{
    if(m1_boot_state()==M1_BOOT_READY || !m1_usb_hw_running())return false;
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
    }
    unsigned size=m1_usb_midi_take(events,sizeof(events));
    if(size && ready())midi_control_receive_usb(events,size);
    unlock(mask);
    midi_control_service();
    if(!midi_control_ready())reported=false;
    if(m1_boot_state()==M1_BOOT_FAILED && !reported) {
        char text[80];size_t length=failure_text(text);
        reported=midi_control_publish(MT_LOG,(const uint8_t *)text,length);
    }
    return update_requested;
}
