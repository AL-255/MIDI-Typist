/* Synthetic-address class/SDK audit only; no clock, PHY, flash or real IRQs. */
#include "m1_usb.h"
#include "m1_usb_hal.h"
#include "m1_usb_power.h"
#include "m1_power_gpio.h"
#include "midi_control.h"
#include "scan_stream.h"
#include "keyboard_telemetry.h"
#include <string.h>
static usbd_core_type device;
/* SDK public header misspells this function's declaration. */
void usb_ept_default_init(usbd_core_type *dev);
void m1_test_usb_init(unsigned high_speed)
{
    memset(&device,0,sizeof(device));device.usb_reg=OTG2_GLOBAL;
    device.class_handler=&m1_usb_class;device.desc_handler=&m1_usb_descriptors;
    device.speed=high_speed?USB_HIGH_SPEED:USB_FULL_SPEED;
    device.conn_state=USB_CONN_STATE_CONFIGURED;device.dev_config=1;
    usb_ept_default_init(&device);
    m1_usb_bind(&device);
    usbd_ept_open(&device,0,EPT_CONTROL_TYPE,64);
    usbd_ept_open(&device,0x80,EPT_CONTROL_TYPE,64);
    m1_usb_class.init_handler(&device);
}
void m1_test_usb_setup(unsigned type,unsigned request,unsigned value,unsigned index_length)
{
    uint8_t *p=(uint8_t *)device.setup_buffer;
    p[0]=type;p[1]=request;p[2]=value;p[3]=value>>8;
    for(unsigned i=0;i<4;++i)p[i+4]=index_length>>(i*8);
    usbd_core_setup_handler(&device,0);
}
uintptr_t m1_test_usb_get(unsigned field)
{
    switch(field) {
        case 0:return (uintptr_t)device.ept_in[1].trans_buf;
        case 1:return device.ept_in[1].total_len;
        case 2:return (uintptr_t)device.ept_in[2].trans_buf;
        case 3:return (uintptr_t)device.ept_out[2].trans_buf;
        case 4:return (uintptr_t)device.ept_in[0].trans_buf;
        case 5:return (uintptr_t)device.ept_out[0].trans_buf;
        case 6:return device.ept_in[0].rem0_len;
        case 7:return device.ept_out[2].total_len;
        case 8:return device.ept0_sts;
        case 9:return (uintptr_t)device.ept_in[3].trans_buf;
        default:return 0;
    }
}
void m1_test_usb_in(unsigned endpoint) { usbd_core_in_handler(&device,endpoint); }
void m1_test_usb_out(unsigned endpoint,unsigned size)
{ device.ept_out[endpoint].trans_len=size;usbd_core_out_handler(&device,endpoint); }
void m1_test_usb_event(unsigned event) { m1_usb_class.event_handler(&device,event); }
void m1_test_usb_sof(unsigned count)
{ while(count--)m1_usb_class.sof_handler(&device); }
static keyboard_app_t app;
static keyboard_raw_t raw;
static keyboard_midi_t midi;
static keyboard_menu_t menu;
static keyboard_calibration_t cal;
static keyboard_telemetry_status_t status={.storage_slot=255};
static uint32_t now,epoch;
static uint32_t millis(void) { return now; }
static uint32_t lock(void) { uint32_t saved=__get_PRIMASK();__disable_irq();return saved; }
static void unlock(uint32_t saved) { __set_PRIMASK(saved); }
static bool command(const char *line)
{
    if(!strcmp(line,"stream gui")) { scan_stream_gui();return true; }
    return keyboard_app_command(&app,line,now,true,&status.ack,&status.result);
}
void m1_test_usb_control_init(void)
{
    static const midi_control_port_t port={millis,m1_usb_ready,m1_usb_midi_send,lock,unlock};
    uint16_t samples[82],lo[82],hi[82];
    for(unsigned i=0;i<82;++i) { samples[i]=3900+i;lo[i]=1000;hi[i]=4096; }
    now=0;epoch=m1_usb_generation();
    keyboard_app_init(&app,&raw,&midi,&menu,&cal,NULL);
    keyboard_app_frame(&app,samples,82,1,lo,hi,true,now);
    scan_stream_init();midi_control_init(&port);midi_control_command_handler(command);
}
void m1_test_usb_service(void)
{
    ++now;
    if(epoch!=m1_usb_generation()) {
        epoch=m1_usb_generation();midi_control_usb_reset();keyboard_app_invalidate(&app,now);
    }
    uint8_t events[M1_USB_HS_PACKET];unsigned n=m1_usb_midi_take(events,sizeof(events));
    if(n)midi_control_receive_usb(events,n);
    midi_control_service();
}
bool m1_test_usb_publish(void)
{
    uint8_t frame[MT_GUI_SIZE(82,30)];
    size_t n=keyboard_telemetry_encode(&app,&status,frame,sizeof(frame));
    return n && midi_control_publish(MT_SNAPSHOT,frame,n);
}
__attribute__((used,section(".test_exports")))
const void *const m1_test_exports[]={
    m1_test_usb_init,m1_test_usb_setup,m1_test_usb_get,m1_test_usb_in,m1_test_usb_out,
    m1_test_usb_event,m1_test_usb_sof,m1_usb_ready,m1_usb_drained,m1_usb_in_idle,m1_usb_hid_send,
    m1_usb_midi_send,m1_usb_midi_take,m1_usb_generation,m1_usb_errors,m1_usb_leds,m1_usb_consumer_send,
    m1_test_usb_control_init,m1_test_usb_service,m1_test_usb_publish,
    m1_usb_hw_start,m1_usb_hw_stop,m1_usb_hw_running,m1_usb_hw_irq,
    m1_usb_power_down,m1_usb_power_ready,m1_power_gpio_prepare,m1_power_gpio_restore
};
