#include "m1_usb.h"
/* Project-owned class descriptors, not factory descriptor bytes. Identity is
 * for this converted keyboard only; this is not an allocated product VID/PID.
 * No fabricated serial: iSerialNumber stays zero until board identity is bound. */
#define LO(x) ((x)&255u)
#define HI(x) (((x)>>8)&255u)
static uint8_t device[]={18,1,0,2,0xef,2,1,64,0x51,0x31,0x30,0x50,0,1,1,2,0,1};
static uint8_t qualifier[]={10,6,0,2,0xef,2,1,64,1,0};
static uint8_t report[]={
    5,1,9,6,0xa1,1,5,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,
    0x75,1,0x95,8,0x81,2,0x75,8,0x95,1,0x81,1,
    0x19,4,0x29,0xdf,0x75,1,0x95,0xdc,0x81,2,0x95,4,0x81,1,
    /* Num/Caps/Scroll/Compose/Kana LED output, one byte, no report ID. */
    5,8,0x19,1,0x29,5,0x95,5,0x91,2,0x95,3,0x91,1,0xc0
};
_Static_assert(KEYBOARD_NKRO_REPORT_BYTES==30,"USB descriptor report budget");
static uint8_t consumer[]={
    5,0x0c,9,1,0xa1,1,0x15,0,0x26,LO(M1_USB_CONSUMER_USAGE_MAX),HI(M1_USB_CONSUMER_USAGE_MAX),
    0x19,0,0x2a,LO(M1_USB_CONSUMER_USAGE_MAX),HI(M1_USB_CONSUMER_USAGE_MAX),
    0x75,16,0x95,1,0x81,0,0xc0
};
#define CONFIG(type,packet,interval) \
    9,type,191,0,4,1,0,0x80,250, \
    9,4,0,0,1,3,0,0,0, \
    9,0x21,0x11,1,0,1,0x22,sizeof(report),0, \
    7,5,0x81,3,30,0,interval, \
    8,11,1,2,1,1,0,4, \
    9,4,1,0,0,1,1,0,4, \
    9,0x24,1,0,1,9,0,1,2, \
    9,4,2,0,2,1,3,0,4, \
    7,0x24,1,0,1,0x61,0, \
    6,0x24,2,1,1,4, 6,0x24,2,2,2,4, \
    9,0x24,3,1,3,1,2,1,4, 9,0x24,3,2,4,1,1,1,4, \
    6,0x24,2,1,5,5, 6,0x24,2,2,6,5, \
    9,0x24,3,1,7,1,6,1,5, 9,0x24,3,2,8,1,5,1,5, \
    9,5,2,2,LO(packet),HI(packet),0,0,0, 6,0x25,1,2,1,5, \
    9,5,0x82,2,LO(packet),HI(packet),0,0,0, 6,0x25,1,2,3,7, \
    9,4,M1_USB_CONSUMER_INTERFACE,0,1,3,0,0,0, \
    9,0x21,0x11,1,0,1,0x22,sizeof(consumer),0, \
    7,5,M1_USB_CONSUMER_IN,3,2,0,interval
static uint8_t fs[]={CONFIG(2,M1_USB_FS_PACKET,1)};
static uint8_t hs[]={CONFIG(2,M1_USB_HS_PACKET,1)};
static uint8_t other_fs[]={CONFIG(7,M1_USB_FS_PACKET,1)};
static uint8_t other_hs[]={CONFIG(7,M1_USB_HS_PACKET,1)};
_Static_assert(sizeof(fs)==191 && sizeof(hs)==191,"configuration descriptor length");
static uint8_t lang[]={4,3,9,4};
static uint8_t maker[]={24,3,'M',0,'I',0,'D',0,'I',0,'-',0,'T',0,'y',0,'p',0,'i',0,'s',0,'t',0};
/* Short enough that ALSA's product + jack name keeps the full Control label. */
static uint8_t product[]={20,3,'M',0,'1',0,' ',0,'V',0,'5',0,' ',0,'T',0,'M',0,'R',0};
static uint8_t performance[]={24,3,'P',0,'e',0,'r',0,'f',0,'o',0,'r',0,'m',0,'a',0,'n',0,'c',0,'e',0};
static uint8_t control[]={40,3,'M',0,'I',0,'D',0,'I',0,'-',0,'T',0,'y',0,'p',0,'i',0,'s',0,'t',0,
    ' ',0,'C',0,'o',0,'n',0,'t',0,'r',0,'o',0,'l',0};
#define DESC(name) static usbd_desc_t *get_##name(void) { static usbd_desc_t d={sizeof(name),name};return &d; }
DESC(device) DESC(qualifier) DESC(fs) DESC(hs) DESC(other_fs) DESC(other_hs)
DESC(lang) DESC(maker) DESC(product) DESC(performance) DESC(control) DESC(report) DESC(consumer)
static usbd_desc_t *no_serial(void) { return NULL; }
usbd_desc_handler m1_usb_descriptors={
    get_device,get_qualifier,get_fs,get_other_fs,get_lang,get_maker,get_product,
    no_serial,get_control,get_performance,get_hs
};
usbd_desc_t *m1_usb_report_descriptor(void) { return get_report(); }
usbd_desc_t *m1_usb_hid_descriptor(void) { static usbd_desc_t d={9,fs+18};return &d; }
usbd_desc_t *m1_usb_consumer_report_descriptor(void) { return get_consumer(); }
usbd_desc_t *m1_usb_consumer_hid_descriptor(void) { static usbd_desc_t d={9,fs+175};return &d; }
usbd_desc_t *m1_usb_other_descriptor(bool high_speed)
{ return high_speed?get_other_fs():get_other_hs(); }
