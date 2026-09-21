#include "at32_usb.h"
#include <string.h>

/* Keep factory VID/PID for this user-installed board replacement. This is not
 * an allocation for other products: a new board must choose its own identity. */
static uint8_t device[]={18,1,0,2,0xef,2,1,64,0x51,0x31,0x2d,0x50,0,1,1,2,3,1};
static uint8_t qualifier[]={10,6,0,2,0xef,2,1,64,1,0};
const uint8_t mt_at32_hid_report[]={
    0x05,1,0x09,6,0xa1,1,0x05,7,0x19,0xe0,0x29,0xe7,
    0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
    0x75,8,0x95,1,0x81,1,
    0x19,4,0x29,0x73,0x15,0,0x25,1,0x75,1,0x95,0x70,0x81,2,0xc0
};
const uint16_t mt_at32_hid_report_size=sizeof(mt_at32_hid_report);
static const uint8_t configuration_template[]={
    9,2,166,0,3,1,0,0x80,250,
    9,4,0,0,1,3,0,0,0,
    9,0x21,0x11,1,0,1,0x22,sizeof(mt_at32_hid_report),0,
    7,5,MT_AT32_HID_IN,3,16,0,1,
    8,11,1,2,1,1,0,4,
    9,4,1,0,0,1,1,0,4,
    9,0x24,1,0,1,9,0,1,2,
    9,4,2,0,2,1,3,0,4,
    7,0x24,1,0,1,97,0,
    6,0x24,2,1,1,4, 6,0x24,2,2,2,4,
    9,0x24,3,1,3,1,2,1,4, 9,0x24,3,2,4,1,1,1,4,
    6,0x24,2,1,5,5, 6,0x24,2,2,6,5,
    9,0x24,3,1,7,1,6,1,5, 9,0x24,3,2,8,1,5,1,5,
    9,5,MT_AT32_MIDI_OUT,2,64,0,0,0,0, 6,0x25,1,2,1,5,
    9,5,MT_AT32_MIDI_IN,2,64,0,0,0,0, 6,0x25,1,2,3,7
};
_Static_assert(sizeof(configuration_template)==166,"configuration length");
_Static_assert(sizeof(keyboard_report_t)==16,"descriptor is 16-byte NKRO");
static uint8_t configuration[166],other[166],string_buffer[128];
static usbd_desc_t descriptor;
static usbd_desc_t *wrap(uint8_t *bytes,uint16_t length)
{ descriptor.descriptor=bytes;descriptor.length=length;return &descriptor; }
static usbd_desc_t *config(bool high,bool opposite)
{
    uint8_t *bytes=opposite?other:configuration;
    memcpy(bytes,configuration_template,sizeof(configuration));
    bytes[1]=opposite?7:2;
    for(unsigned offset=0;offset<sizeof(configuration);offset+=bytes[offset]) {
        if(bytes[offset+1]==5 && bytes[offset+3]==2) {
            unsigned size=high?MT_AT32_HS_PACKET:MT_AT32_FS_PACKET;
            bytes[offset+4]=size;bytes[offset+5]=size>>8;
        }
    }
    return wrap(bytes,sizeof(configuration));
}
static usbd_desc_t *device_desc(void) { return wrap(device,sizeof(device)); }
static usbd_desc_t *qualifier_desc(void) { return wrap(qualifier,sizeof(qualifier)); }
static usbd_desc_t *fs_desc(void) { return config(false,false); }
static usbd_desc_t *hs_desc(void) { return config(true,false); }
static usbd_desc_t *other_desc(void) { return config(false,true); }
static usbd_desc_t *language(void)
{ static uint8_t lang[]={4,3,9,4};return wrap(lang,sizeof(lang)); }
static usbd_desc_t *string(const char *ascii)
{
    size_t length=strlen(ascii);
    if(length>(sizeof(string_buffer)-2)/2)length=(sizeof(string_buffer)-2)/2;
    string_buffer[0]=2+2*length;string_buffer[1]=3;
    for(size_t i=0;i<length;++i) { string_buffer[2+2*i]=ascii[i];string_buffer[3+2*i]=0; }
    return wrap(string_buffer,string_buffer[0]);
}
static usbd_desc_t *manufacturer(void) { return string("MIDI-Typist"); }
static usbd_desc_t *product(void) { return string("FUN60 PRO MIDI-Typist"); }
static usbd_desc_t *performance(void) { return string("MIDI-Typist Performance"); }
static usbd_desc_t *control(void) { return string("MIDI-Typist Control"); }
static usbd_desc_t *serial(void)
{
    /* Official SDK MCU_ID1/2/3: a stable chip identifier, not a fabricated
     * factory serial. GUI distinguishes USB identity from factory data. */
    const volatile uint32_t *uid=(const volatile uint32_t *)0x1ffff7e8u;
    const char hex[]="0123456789ABCDEF";char text[25];
    for(unsigned i=0;i<3;++i) {
        uint32_t value=uid[i];
        for(unsigned j=0;j<8;++j)text[8*i+j]=hex[(value>>(28-4*j))&15];
    }
    text[24]=0;return string(text);
}
usbd_desc_handler mt_at32_descriptors={
    .get_device_descriptor=device_desc,.get_device_qualifier=qualifier_desc,
    .get_device_configuration=fs_desc,.get_device_other_speed=other_desc,
    .get_device_lang_id=language,.get_device_manufacturer_string=manufacturer,
    .get_device_product_string=product,.get_device_serial_string=serial,
    .get_device_interface_string=control,.get_device_config_string=performance,
    .get_hs_device_configuration=hs_desc
};
