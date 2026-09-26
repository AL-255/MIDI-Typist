#include "m1_usb.h"
#include "usbd_sdr.h"
#include <string.h>

static struct {
    usbd_core_type *dev;
    volatile bool configured,suspended,fault,hid_busy,midi_busy,receive_pending,led_pending;
    volatile bool hid_seen,idle_deferred;
    volatile uint8_t idle,next_idle,leds;
    volatile uint16_t received;
    volatile uint32_t generation,errors,idle_ticks;
    volatile bool consumer_busy,consumer_seen,consumer_deferred;
    volatile uint8_t consumer_idle,consumer_next;
    volatile uint32_t consumer_ticks;
    _Alignas(4) uint8_t consumer[4];
    _Alignas(4) uint8_t hid[32],midi[M1_USB_HS_PACKET],rx[M1_USB_HS_PACKET],control[64];
} s;
static unsigned packet_size(void)
{ return s.dev->speed==USB_HIGH_SPEED?M1_USB_HS_PACKET:M1_USB_FS_PACKET; }
static void invalidated(void)
{
    s.configured=s.suspended=s.fault=s.hid_busy=s.midi_busy=false;
    s.receive_pending=s.led_pending=false;s.received=0;s.idle=0;s.leds=0;
    s.hid_seen=s.idle_deferred=false;s.next_idle=0;
    s.idle_ticks=0;memset(s.hid,0,sizeof(s.hid));++s.generation;
    s.consumer_busy=s.consumer_seen=s.consumer_deferred=false;
    s.consumer_idle=s.consumer_next=0;s.consumer_ticks=0;
    memset(s.consumer,0,sizeof(s.consumer));
}
void m1_usb_bind(usbd_core_type *device) { invalidated();s.dev=device;s.errors=0; }
bool m1_usb_ready(void)
{ return s.dev && s.configured && !s.suspended && !s.fault && s.dev->conn_state==USB_CONN_STATE_CONFIGURED; }
uint32_t m1_usb_generation(void) { return s.generation; }
uint32_t m1_usb_errors(void) { return s.errors; }
uint8_t m1_usb_leds(void) { return s.leds; }
bool m1_usb_in_idle(void) { return !s.hid_busy && !s.midi_busy && !s.consumer_busy; }
bool m1_usb_drained(void) { return m1_usb_ready() && m1_usb_in_idle(); }
static void fault(void) { ++s.errors;s.fault=true;++s.generation; }
static usb_sts_type unsupported(usbd_core_type *dev)
{ usbd_ctrl_unsupport(dev);return USB_FAIL; }
static void send_control(usbd_core_type *dev,usbd_desc_t *desc,unsigned requested)
{ usbd_ctrl_send(dev,desc->descriptor,requested<desc->length?requested:desc->length); }
static bool stop_in(usbd_core_type *dev,unsigned ep)
{
    usbd_ept_in_check_fifo(dev,ep|128u);
    if(USB_INEPT(dev->usb_reg,ep)->diepctl_bit.eptena ||
       USB_INEPT(dev->usb_reg,ep)->diepctl_bit.eptdis)return false;
    /* Already-disabled endpoints bypass the SDK check helper's flush. They
     * can still contain data from an aborted report: always flush explicitly. */
    usbd_flush_tx_fifo(dev,ep);
    if(dev->usb_reg->grstctl_bit.txfflsh)return false;
    OTG_DEVICE(dev->usb_reg)->diepempmsk&=~(1u<<ep);
    USB_INEPT(dev->usb_reg,ep)->diepint=0xffu;
    return true;
}
static usb_sts_type init(void *device)
{
    usbd_core_type *dev=device;
    if(dev!=s.dev || dev->dma_en)return USB_FAIL;
    bool hid_stopped=stop_in(dev,1),midi_stopped=stop_in(dev,2),consumer_stopped=stop_in(dev,3);
    if(!hid_stopped || !midi_stopped || !consumer_stopped) { s.configured=false;fault();return USB_FAIL; }
    invalidated();s.configured=true;
    usbd_ept_open(dev,M1_USB_HID_IN,EPT_INT_TYPE,KEYBOARD_NKRO_REPORT_BYTES);
    usbd_ept_open(dev,M1_USB_MIDI_IN,EPT_BULK_TYPE,packet_size());
    usbd_ept_open(dev,M1_USB_MIDI_OUT,EPT_BULK_TYPE,packet_size());
    usbd_ept_open(dev,M1_USB_CONSUMER_IN,EPT_INT_TYPE,2);
    usbd_ept_recv(dev,M1_USB_MIDI_OUT,s.rx,packet_size());
    return USB_OK;
}
static usb_sts_type clear(void *device)
{
    usbd_core_type *dev=device;
    if(dev!=s.dev)return USB_FAIL;
    bool hid_stopped=stop_in(dev,1),midi_stopped=stop_in(dev,2),consumer_stopped=stop_in(dev,3);
    usbd_ept_close(dev,M1_USB_HID_IN);usbd_ept_close(dev,M1_USB_MIDI_IN);
    usbd_ept_close(dev,M1_USB_MIDI_OUT);
    usbd_ept_close(dev,M1_USB_CONSUMER_IN);
    usb_flush_rx_fifo(dev->usb_reg);
    bool rx_flushed=!dev->usb_reg->grstctl_bit.rxfflsh;
    invalidated();
    if(!hid_stopped || !midi_stopped || !consumer_stopped || !rx_flushed) { fault();return USB_FAIL; }
    return USB_OK;
}
static usb_sts_type setup(void *device,usb_setup_type *q)
{
    usbd_core_type *dev=device;s.led_pending=false;
    if(dev!=s.dev)return unsupported(dev);
    /* Endpoint halt changes invalidate application output ownership. Abort
     * pending IN safely; the epoch tells the application to send neutral and
     * reset its MIDI session, not to count the aborted transfer as delivered. */
    if(q->bmRequestType==2 && (q->bRequest==1 || q->bRequest==3) &&
       !q->wValue && !q->wLength &&
       (q->wIndex==M1_USB_HID_IN || q->wIndex==M1_USB_MIDI_IN || q->wIndex==M1_USB_MIDI_OUT || q->wIndex==M1_USB_CONSUMER_IN)) {
        ++s.generation;
        unsigned ep=q->wIndex&127u;
        if(q->wIndex&128u) {
            if(!stop_in(dev,ep)) { fault();return USB_FAIL; }
            if(ep==1) { s.hid_busy=false;s.hid_seen=false; }
            else if(ep==3) { s.consumer_busy=false;s.consumer_seen=false; }
            else s.midi_busy=false;
        }
        return USB_OK;
    }
    if(q->wIndex>=M1_USB_INTERFACES)return unsupported(dev);
    if(q->bmRequestType==0x81 && q->bRequest==6 && q->wIndex==M1_USB_CONSUMER_INTERFACE) {
        usbd_desc_t *d=q->wValue==0x2200?m1_usb_consumer_report_descriptor():
                       q->wValue==0x2100?m1_usb_consumer_hid_descriptor():NULL;
        if(!d || !q->wLength)return unsupported(dev);
        send_control(dev,d,q->wLength);return USB_OK;
    }
    if(q->bmRequestType==0x81 && q->bRequest==6 && !q->wIndex && !(q->wValue&255)) {
        usbd_desc_t *d=q->wValue==0x2200?m1_usb_report_descriptor():
                       q->wValue==0x2100?m1_usb_hid_descriptor():NULL;
        if(!d || !q->wLength)return unsupported(dev);
        send_control(dev,d,q->wLength);return USB_OK;
    }
    if(q->bmRequestType==0x81 && q->bRequest==10 && !q->wValue && q->wLength==1) {
        s.control[0]=0;usbd_ctrl_send(dev,s.control,1);return USB_OK;
    }
    if(q->bmRequestType==1 && q->bRequest==11 && !q->wValue && !q->wLength)return USB_OK;
    if(q->bmRequestType==0x81 && q->bRequest==0 && !q->wValue && q->wLength==2) {
        s.control[0]=s.control[1]=0;usbd_ctrl_send(dev,s.control,2);return USB_OK;
    }
    if(q->wIndex==M1_USB_CONSUMER_INTERFACE && s.configured) {
        if(q->bmRequestType==0xa1 && q->bRequest==1 && q->wValue==0x100 && q->wLength) {
            memcpy(s.control,s.consumer,2);usbd_ctrl_send(dev,s.control,q->wLength<2?q->wLength:2);return USB_OK;
        }
        if(q->bmRequestType==0xa1 && q->bRequest==2 && !q->wValue && q->wLength==1) {
            s.control[0]=s.consumer_deferred?s.consumer_next:s.consumer_idle;
            usbd_ctrl_send(dev,s.control,1);return USB_OK;
        }
        if(q->bmRequestType==0x21 && q->bRequest==10 && !(q->wValue&255) && !q->wLength) {
            unsigned unit=4u*(dev->speed==USB_HIGH_SPEED?8u:1u),period=s.consumer_idle*unit;
            if(s.consumer_idle && s.consumer_ticks<period && period-s.consumer_ticks<unit) {
                s.consumer_next=q->wValue>>8;s.consumer_deferred=true;
            } else { s.consumer_idle=q->wValue>>8;s.consumer_deferred=false; }
            return USB_OK;
        }
        return unsupported(dev);
    }
    if(q->wIndex || !s.configured)return unsupported(dev);
    if(q->bmRequestType==0xa1 && q->bRequest==1 && q->wLength &&
       (q->wValue==0x100 || q->wValue==0x200)) {
        unsigned len=q->wValue==0x100?KEYBOARD_NKRO_REPORT_BYTES:1;
        if(len==1)s.control[0]=s.leds;else memcpy(s.control,s.hid,len);
        usbd_ctrl_send(dev,s.control,q->wLength<len?q->wLength:len);return USB_OK;
    }
    if(q->bmRequestType==0x21 && q->bRequest==9 && q->wValue==0x200 && q->wLength==1) {
        s.led_pending=true;usbd_ctrl_recv(dev,s.control,1);return USB_OK;
    }
    if(q->bmRequestType==0xa1 && q->bRequest==2 && !q->wValue && q->wLength==1) {
        s.control[0]=s.idle_deferred?s.next_idle:s.idle;usbd_ctrl_send(dev,s.control,1);return USB_OK;
    }
    if(q->bmRequestType==0x21 && q->bRequest==10 && !(q->wValue&255) && !q->wLength) {
        unsigned unit=4u*(dev->speed==USB_HIGH_SPEED?8u:1u),period=s.idle*unit;
        /* HID 1.11 7.2.4: preserve elapsed time. A change in the last four
         * milliseconds of the current period takes effect after that report. */
        if(s.idle && s.idle_ticks<period && period-s.idle_ticks<unit) {
            s.next_idle=q->wValue>>8;s.idle_deferred=true;
        } else { s.idle=q->wValue>>8;s.idle_deferred=false; }
        return USB_OK;
    }
    /* Report-only HID: no boot protocol or audio-stream alternates. */
    return unsupported(dev);
}
static usb_sts_type control_in(void *device) { (void)device;return USB_OK; }
static usb_sts_type control_out(void *device)
{
    if(device!=s.dev)return USB_FAIL;
    if(s.led_pending) {
        if(usbd_get_recv_len(s.dev,0)==1)s.leds=s.control[0]&31u;
        else fault();
    }
    s.led_pending=false;return USB_OK;
}
static usb_sts_type in(void *device,uint8_t endpoint)
{
    if(device!=s.dev)return USB_FAIL;
    if(endpoint==1) {
        s.hid_busy=false;s.idle_ticks=0;
        if(s.idle_deferred) { s.idle=s.next_idle;s.idle_deferred=false; }
    }
    if(endpoint==2)s.midi_busy=false;
    if(endpoint==3) {
        s.consumer_busy=false;s.consumer_ticks=0;
        if(s.consumer_deferred) { s.consumer_idle=s.consumer_next;s.consumer_deferred=false; }
    }
    return USB_OK;
}
static usb_sts_type out(void *device,uint8_t endpoint)
{
    if(device!=s.dev || endpoint!=2)return USB_FAIL;
    unsigned len=usbd_get_recv_len(s.dev,endpoint);
    if(s.receive_pending || len>packet_size() || len%4) { fault();return USB_FAIL; }
    if(len) { s.received=len;s.receive_pending=true; }
    else usbd_ept_recv(s.dev,M1_USB_MIDI_OUT,s.rx,packet_size());
    return USB_OK;
}
static usb_sts_type sof(void *device)
{
    if(device==s.dev && m1_usb_ready() && !s.consumer_busy) {
        unsigned interval=s.consumer_idle*4u*(s.dev->speed==USB_HIGH_SPEED?8u:1u);
        if(s.consumer_ticks<UINT32_MAX)++s.consumer_ticks;
        if(s.consumer_idle && s.consumer_ticks>=interval && !s.dev->ept_in[3].stall) {
            s.consumer_busy=s.consumer_seen=true;
            usbd_ept_send(s.dev,M1_USB_CONSUMER_IN,s.consumer,2);
        }
    }
    if(device==s.dev && m1_usb_ready() && !s.hid_busy) {
        unsigned interval=s.idle*4u*(s.dev->speed==USB_HIGH_SPEED?8u:1u);
        if(s.idle_ticks<UINT32_MAX)++s.idle_ticks;
        if(s.idle && s.idle_ticks>=interval && !s.dev->ept_in[1].stall) {
            s.hid_busy=s.hid_seen=true;
            usbd_ept_send(s.dev,M1_USB_HID_IN,s.hid,KEYBOARD_NKRO_REPORT_BYTES);
        }
    }
    return USB_OK;
}
static usb_sts_type event(void *device,usbd_event_type event)
{
    if(device!=s.dev)return USB_FAIL;
    if(event==USBD_RESET_EVENT || event==USBD_DISCONNECT_EVNET)invalidated();
    else if(event==USBD_SUSPEND_EVENT) { s.suspended=true;++s.generation; }
    else if(event==USBD_WAKEUP_EVENT) { s.suspended=false;++s.generation; }
    else if(event==USBD_ERR_EVENT)fault();
    return USB_OK;
}
usbd_class_handler m1_usb_class={init,clear,setup,control_in,control_out,in,out,sof,event,NULL};
bool m1_usb_hid_send(const keyboard_report_t *report)
{
    if(!report || __get_IPSR())return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    bool ok=m1_usb_ready() && !s.hid_busy && !s.dev->ept_in[1].stall;
    if(ok && (!s.hid_seen || memcmp(s.hid,report,sizeof(*report)))) {
        memcpy(s.hid,report,sizeof(*report));s.hid_busy=s.hid_seen=true;
        usbd_ept_send(s.dev,M1_USB_HID_IN,s.hid,sizeof(*report));
    }
    __set_PRIMASK(mask);return ok;
}
bool m1_usb_midi_send(const uint8_t *events,uint32_t size)
{
    if(!events || !size || size%4 || __get_IPSR())return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    bool ok=m1_usb_ready() && !s.midi_busy && size<=packet_size() && !s.dev->ept_in[2].stall;
    if(ok) { memcpy(s.midi,events,size);s.midi_busy=true;usbd_ept_send(s.dev,M1_USB_MIDI_IN,s.midi,size); }
    __set_PRIMASK(mask);return ok;
}
bool m1_usb_consumer_send(uint16_t usage)
{
    if(usage>M1_USB_CONSUMER_USAGE_MAX || __get_IPSR())return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    bool ok=m1_usb_ready() && !s.consumer_busy && !s.dev->ept_in[3].stall;
    uint8_t data[2]={(uint8_t)usage,(uint8_t)(usage>>8)};
    if(ok && (!s.consumer_seen || memcmp(s.consumer,data,2))) {
        memcpy(s.consumer,data,2);s.consumer_busy=s.consumer_seen=true;
        usbd_ept_send(s.dev,M1_USB_CONSUMER_IN,s.consumer,2);
    }
    __set_PRIMASK(mask);return ok;
}
unsigned m1_usb_midi_take(uint8_t *events,unsigned capacity)
{
    if(!events || __get_IPSR())return 0;
    uint32_t mask=__get_PRIMASK();__disable_irq();unsigned size=0;
    if(m1_usb_ready() && s.receive_pending && capacity>=s.received) {
        size=s.received;memcpy(events,s.rx,size);s.receive_pending=false;s.received=0;
        usbd_ept_recv(s.dev,M1_USB_MIDI_OUT,s.rx,packet_size());
    }
    __set_PRIMASK(mask);return size;
}

/* Validate before the SDK indexes endpoint arrays using host wIndex. The
 * linker wrappers leave the upstream implementation and notices untouched. */
usb_sts_type __real_usbd_endpoint_request(usbd_core_type *dev);
usb_sts_type __wrap_usbd_endpoint_request(usbd_core_type *dev)
{
    const usb_setup_type *q=&dev->setup;s.led_pending=false;
    bool endpoint=q->wIndex==0 || q->wIndex==0x80 || q->wIndex==M1_USB_HID_IN ||
                  q->wIndex==M1_USB_MIDI_IN || q->wIndex==M1_USB_MIDI_OUT || q->wIndex==M1_USB_CONSUMER_IN;
    bool request=(q->bmRequestType==0x82 && q->bRequest==0 && !q->wValue && q->wLength==2) ||
        (q->bmRequestType==2 && (q->bRequest==1 || q->bRequest==3) &&
         (q->wIndex&127u) && !q->wValue && !q->wLength);
    if(dev!=s.dev || !endpoint || !request)return unsupported(dev);
    return __real_usbd_endpoint_request(dev);
}
usb_sts_type __real_usbd_device_request(usbd_core_type *dev);
usb_sts_type __wrap_usbd_device_request(usbd_core_type *dev)
{
    const usb_setup_type *q=&dev->setup;s.led_pending=false;
    if(dev!=s.dev)return unsupported(dev);
    /* The SDK serves these only while negotiated HS. A dual-speed device
     * must also describe its other speed while attached to an FS host. */
    if(q->bmRequestType==0x80 && q->bRequest==6 && !q->wIndex &&
       (q->wValue==0x600 || q->wValue==0x700) && q->wLength) {
        usbd_desc_t *d=q->wValue==0x600?m1_usb_descriptors.get_device_qualifier():
            m1_usb_other_descriptor(dev->speed==USB_HIGH_SPEED);
        send_control(dev,d,q->wLength);return USB_OK;
    }
    unsigned type=q->wValue>>8,index=q->wValue&255u;
    bool descriptor=q->bmRequestType==0x80 && q->bRequest==6 && q->wLength &&
        (((type==1 || type==2) && !index && !q->wIndex) ||
         (type==3 && (index==0 || index==1 || index==2 || index==4 || index==5) &&
          (!q->wIndex || q->wIndex==0x409)));
    bool request=descriptor || (!q->wIndex && (
        (q->bmRequestType==0x80 && !q->bRequest && !q->wValue && q->wLength==2) ||
        (q->bmRequestType==0x80 && q->bRequest==8 && !q->wValue && q->wLength==1) ||
        (!q->bmRequestType && q->bRequest==5 && q->wValue<=127 && !q->wLength) ||
        (!q->bmRequestType && q->bRequest==9 && q->wValue<=1 && !q->wLength)));
    /* No advertised remote wake, test mode, factory vendor command or
     * boot-entry path in this class. Never let an unsupported request alter
     * the SDK's remote-wake/test-mode fields before stalling. */
    if(!request)return unsupported(dev);
    return __real_usbd_device_request(dev);
}
