#ifndef MIDI_TYPIST_AT32_USB_CONF_H
#define MIDI_TYPIST_AT32_USB_CONF_H
#include <stddef.h>
#include "at32f402_405_conf.h"
#define USE_OTG_DEVICE_MODE
#define USB_OTG_HS
#define USB_VBUS_IGNORE /* board PC13, not PB13 (sensor supply), detects cable */
#define USBD_SUPPORT_WINUSB 0
/* Word counts. OTG2 has 1024 words: RX 256, EP0 64, HID 32,
 * MIDI 256, five unused FIFOs 16 each = 688 words. No USB DMA. */
#define USBD2_RX_SIZE 256
#define USBD2_EP0_TX_SIZE 64
#define USBD2_EP1_TX_SIZE 32
#define USBD2_EP2_TX_SIZE 256
#define USBD2_EP3_TX_SIZE 16
#define USBD2_EP4_TX_SIZE 16
#define USBD2_EP5_TX_SIZE 16
#define USBD2_EP6_TX_SIZE 16
#define USBD2_EP7_TX_SIZE 16
/* Required by the SDK's unused OTG1 branch, within its 320-word SRAM. */
#define USBD_RX_SIZE 128
#define USBD_EP0_TX_SIZE 24
#define USBD_EP1_TX_SIZE 20
#define USBD_EP2_TX_SIZE 20
#define USBD_EP3_TX_SIZE 20
#define USBD_EP4_TX_SIZE 20
#define USBD_EP5_TX_SIZE 20
#define USBD_EP6_TX_SIZE 20
#define USBD_EP7_TX_SIZE 20
void usb_delay_ms(uint32_t ms);
void usb_delay_us(uint32_t us);
#endif
