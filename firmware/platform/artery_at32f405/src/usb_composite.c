#include "at32_usb.h"
#include "control_port.h"
#include "defaults.h"
#include "midi_control.h"
#include "scan_stream.h"
#include "usb_core.h"
#include "usbd_int.h"
#include <string.h>

static otg_core_type core;
static volatile bool hid_busy,midi_busy;
static bool initialized;
static uint8_t hid_idle;
static uint32_t hid_sent_at;
/* Vendor FIFO code reads complete words. Pad/align even short EP0 responses. */
static uint8_t hid_tx[16] __attribute__((aligned(4)));
static uint8_t hid_last[16] __attribute__((aligned(4)));
static uint8_t ctrl_reply[16] __attribute__((aligned(4)));
static uint8_t midi_tx[MT_AT32_HS_PACKET] __attribute__((aligned(4)));
static uint8_t midi_rx[MT_AT32_HS_PACKET] __attribute__((aligned(4)));

uint32_t control_port_millis(void) { return at32_board_millis(); }
uint32_t control_port_lock(void)
{ uint32_t previous=__get_PRIMASK();__disable_irq();return previous; }
void control_port_unlock(uint32_t previous) { __set_PRIMASK(previous); }
bool control_port_ready(void)
{ return initialized && usbd_connect_state_get(&core.dev)==USB_CONN_STATE_CONFIGURED; }
static unsigned packet_size(const usbd_core_type *dev)
{ return dev->speed==USB_HIGH_SPEED?MT_AT32_HS_PACKET:MT_AT32_FS_PACKET; }
static void invalidate(void)
{
    hid_busy=midi_busy=false;hid_idle=0;
    memset(hid_last,0,sizeof(hid_last));
    midi_control_usb_reset();scan_stream_usb_reset();
}
static usb_sts_type configure(void *device)
{
    usbd_core_type *dev=device;invalidate();
    usbd_ept_open(dev,MT_AT32_HID_IN,EPT_INT_TYPE,sizeof(hid_tx));
    usbd_ept_open(dev,MT_AT32_MIDI_IN,EPT_BULK_TYPE,packet_size(dev));
    usbd_ept_open(dev,MT_AT32_MIDI_OUT,EPT_BULK_TYPE,packet_size(dev));
    usbd_ept_recv(dev,MT_AT32_MIDI_OUT,midi_rx,packet_size(dev));
    return USB_OK;
}
static usb_sts_type clear(void *device)
{
    usbd_core_type *dev=device;
    usbd_ept_close(dev,MT_AT32_HID_IN);usbd_ept_close(dev,MT_AT32_MIDI_IN);
    usbd_ept_close(dev,MT_AT32_MIDI_OUT);invalidate();return USB_OK;
}
static usb_sts_type answer(usbd_core_type *dev,uint8_t *bytes,unsigned length,const usb_setup_type *s)
{
    if(length>s->wLength)length=s->wLength;
    usbd_ctrl_send(dev,bytes,length);return USB_OK;
}
static usb_sts_type setup(void *device,usb_setup_type *s)
{
    usbd_core_type *dev=device;
    /* The SDK has already changed endpoint halt state before notifying us. */
    if(s->bmRequestType==0x02 && (s->bRequest==1 || s->bRequest==3) &&
       !s->wValue && !s->wLength &&
       (s->wIndex==MT_AT32_HID_IN || s->wIndex==MT_AT32_MIDI_IN || s->wIndex==MT_AT32_MIDI_OUT))
        return USB_OK;
    /* Interfaces have only alternate zero. No HID boot protocol is advertised. */
    if(s->wIndex<3 && s->bmRequestType==0x81 && s->bRequest==10 && !s->wValue && s->wLength==1) {
        ctrl_reply[0]=0;return answer(dev,ctrl_reply,1,s);
    }
    if(s->wIndex<3 && s->bmRequestType==0x01 && s->bRequest==11 && !s->wValue && !s->wLength)
        return USB_OK;
    if(s->wIndex<3 && s->bmRequestType==0x81 && s->bRequest==0 && !s->wValue && s->wLength==2) {
        ctrl_reply[0]=ctrl_reply[1]=0;return answer(dev,ctrl_reply,2,s);
    }
    if(!s->wIndex) {
        if(s->bmRequestType==0x81 && s->bRequest==6 && s->wValue==0x2200)
            return answer(dev,(uint8_t *)mt_at32_hid_report,mt_at32_hid_report_size,s);
        if(s->bmRequestType==0x81 && s->bRequest==6 && s->wValue==0x2100) {
            usbd_desc_t *d=mt_at32_descriptors.get_device_configuration();
            return answer(dev,d->descriptor+18,9,s);
        }
        if(s->bmRequestType==0xa1 && s->bRequest==1 && s->wValue==0x0100) {
            memcpy(ctrl_reply,hid_last,sizeof(hid_last));return answer(dev,ctrl_reply,sizeof(hid_last),s);
        }
        if(s->bmRequestType==0xa1 && s->bRequest==2 && !s->wValue && s->wLength==1) {
            ctrl_reply[0]=hid_idle;return answer(dev,ctrl_reply,1,s);
        }
        if(s->bmRequestType==0x21 && s->bRequest==10 && !(s->wValue&255) && !s->wLength) {
            hid_idle=s->wValue>>8;hid_sent_at=control_port_millis();return USB_OK;
        }
    }
    usbd_ctrl_unsupport(dev);return USB_ERROR;
}

/* Keep the pinned SDK untouched. Its standard dispatcher only supplies a
 * qualifier/other-speed descriptor at HS and indexes endpoint arrays without
 * validating the host's endpoint number. Wrap these two entry points at link
 * time, keeping the actual control transfer state machine in the vendor SDK. */
usb_sts_type __real_usbd_device_request(usbd_core_type *dev);
usb_sts_type __real_usbd_endpoint_request(usbd_core_type *dev);
usb_sts_type __wrap_usbd_device_request(usbd_core_type *dev)
{
    usb_setup_type *s=&dev->setup;
    if(s->bmRequestType==0x80 && s->bRequest==6 &&
       ((s->wValue>>8)==6 || (s->wValue>>8)==7)) {
        if((s->wValue&255) || s->wIndex || !s->wLength) {
            usbd_ctrl_unsupport(dev);return USB_ERROR;
        }
        usbd_desc_t *d;
        if((s->wValue>>8)==6)d=dev->desc_handler->get_device_qualifier();
        else {
            d=dev->speed==USB_HIGH_SPEED?dev->desc_handler->get_device_configuration():
                dev->desc_handler->get_hs_device_configuration();
            d->descriptor[1]=7;
        }
        return answer(dev,d->descriptor,d->length,s);
    }
    return __real_usbd_device_request(dev);
}
usb_sts_type __wrap_usbd_endpoint_request(usbd_core_type *dev)
{
    usb_setup_type *s=&dev->setup;
    bool control=s->wIndex==0 || s->wIndex==0x80;
    bool endpoint=control || s->wIndex==MT_AT32_HID_IN ||
        s->wIndex==MT_AT32_MIDI_IN || s->wIndex==MT_AT32_MIDI_OUT;
    bool status=s->bmRequestType==0x82 && s->bRequest==0 && s->wLength==2;
    bool halt=!control && s->bmRequestType==0x02 &&
        (s->bRequest==1 || s->bRequest==3) && !s->wLength;
    if(!endpoint || s->wValue || !(status || halt)) {
        usbd_ctrl_unsupport(dev);return USB_ERROR;
    }
    return __real_usbd_endpoint_request(dev);
}
static usb_sts_type complete_in(void *device,uint8_t endpoint)
{
    (void)device;
    if(endpoint==(MT_AT32_HID_IN&15))hid_busy=false;
    if(endpoint==(MT_AT32_MIDI_IN&15))midi_busy=false;
    return USB_OK;
}
static usb_sts_type complete_out(void *device,uint8_t endpoint)
{
    usbd_core_type *dev=device;
    if(endpoint==MT_AT32_MIDI_OUT) {
        unsigned length=usbd_get_recv_len(dev,endpoint);
        if(length<=sizeof(midi_rx))midi_control_receive_usb(midi_rx,length);
        usbd_ept_recv(dev,MT_AT32_MIDI_OUT,midi_rx,packet_size(dev));
    }
    return USB_OK;
}
static usb_sts_type noop(void *device) { (void)device;return USB_OK; }
static usb_sts_type event(void *device,usbd_event_type e)
{
    (void)device;
    if(e==USBD_RESET_EVENT || e==USBD_DISCONNECT_EVNET)invalidate();
    return USB_OK;
}
static usbd_class_handler handlers={
    .init_handler=configure,.clear_handler=clear,.setup_handler=setup,
    .ept0_tx_handler=noop,.ept0_rx_handler=noop,.in_handler=complete_in,
    .out_handler=complete_out,.sof_handler=noop,.event_handler=event,.pdata=NULL
};
void usb_delay_us(uint32_t us)
{
    const uint32_t cycles=system_core_clock/1000000u;
    while(us--) { uint32_t start=DWT->CYCCNT;while((uint32_t)(DWT->CYCCNT-start)<cycles)__NOP(); }
}
void usb_delay_ms(uint32_t ms) { while(ms--)usb_delay_us(1000); }
bool at32_usb_init(void)
{
    initialized=false;NVIC_DisableIRQ(OTGHS_IRQn);
    crm_periph_reset(CRM_OTGHS_PERIPH_RESET,TRUE);
    crm_periph_reset(CRM_OTGHS_PERIPH_RESET,FALSE);
    crm_periph_clock_enable(CRM_OTGHS_PERIPH_CLOCK,TRUE);
    crm_pllu_output_set(TRUE);
    uint32_t start=DWT->CYCCNT;
    while(crm_flag_get(CRM_PLLU_STABLE_FLAG)==RESET) {
        if((uint32_t)(DWT->CYCCNT-start)>=FUN60_CLOCK_TIMEOUT_US*(system_core_clock/1000000u))return false;
    }
    crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_PLLU);
    midi_control_init();scan_stream_init();
    if(usbd_init(&core,USB_HIGH_SPEED_CORE_ID,USB_OTG2_ID,&handlers,&mt_at32_descriptors)!=USB_OK)return false;
    initialized=true;
    NVIC_ClearPendingIRQ(OTGHS_IRQn);NVIC_SetPriority(OTGHS_IRQn,2);NVIC_EnableIRQ(OTGHS_IRQn);
    return true;
}
void OTGHS_IRQHandler(void) { usbd_irq_handler(&core); }
bool control_port_write(const uint8_t *events,size_t length)
{
    if(!events || !length || length%4 || length>sizeof(midi_tx))return false;
    uint32_t irq=control_port_lock();bool accepted=false;
    if(control_port_ready() && !midi_busy) {
        memcpy(midi_tx,events,length);midi_busy=true;
        usbd_ept_send(&core.dev,MT_AT32_MIDI_IN,midi_tx,length);accepted=true;
    }
    control_port_unlock(irq);return accepted;
}
bool at32_usb_midi(uint8_t cin,uint8_t status,uint8_t data1,uint8_t data2)
{
    uint8_t event_bytes[4]={cin,status,data1,data2};
    return control_port_write(event_bytes,sizeof(event_bytes));
}
bool at32_usb_keyboard(const keyboard_report_t *report)
{
    if(!report)return false;
    uint32_t irq=control_port_lock();bool accepted=false;
    if(control_port_ready() && !hid_busy) {
        memcpy(hid_tx,report,sizeof(hid_tx));memcpy(hid_last,report,sizeof(hid_last));
        hid_busy=true;hid_sent_at=control_port_millis();
        usbd_ept_send(&core.dev,MT_AT32_HID_IN,hid_tx,sizeof(hid_tx));accepted=true;
    }
    control_port_unlock(irq);return accepted;
}
void at32_usb_service(void)
{
    uint32_t irq=control_port_lock();
    if(control_port_ready() && !hid_busy && hid_idle &&
       (uint32_t)(control_port_millis()-hid_sent_at)>=4u*hid_idle) {
        memcpy(hid_tx,hid_last,sizeof(hid_tx));hid_busy=true;hid_sent_at=control_port_millis();
        usbd_ept_send(&core.dev,MT_AT32_HID_IN,hid_tx,sizeof(hid_tx));
    }
    control_port_unlock(irq);
}
