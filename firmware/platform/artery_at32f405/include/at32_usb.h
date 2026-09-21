#ifndef MIDI_TYPIST_AT32_USB_H
#define MIDI_TYPIST_AT32_USB_H
#include "keyboard.h"
#include "usbd_core.h"
#define MT_AT32_HID_IN 0x81u
#define MT_AT32_MIDI_IN 0x82u
#define MT_AT32_MIDI_OUT 0x02u
#define MT_AT32_FS_PACKET 64u
#define MT_AT32_HS_PACKET 512u
extern usbd_desc_handler mt_at32_descriptors;
extern const uint8_t mt_at32_hid_report[];
extern const uint16_t mt_at32_hid_report_size;
/* Platform init assumes board clocks are configured and DWT is running. */
bool at32_usb_init(void);
void at32_usb_service(void);
bool at32_usb_keyboard(const keyboard_report_t *report);
bool at32_usb_midi(uint8_t cin,uint8_t status,uint8_t data1,uint8_t data2);
/* Board time source used by both the protocol and HID idle scheduling. */
uint32_t at32_board_millis(void);
#endif
