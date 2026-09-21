#ifndef MIDI_TYPIST_ARTERY_USB_CONF_H
#define MIDI_TYPIST_ARTERY_USB_CONF_H
#include <stddef.h>
#include "at32f402_405.h"
#include "at32f402_405_usb.h"
#define USE_OTG_DEVICE_MODE
#define USB_OTG_HS
#define USB_VBUS_IGNORE
#define USB_EPT_MAX_NUM 8
/* FIFO sizes in 32-bit words. HS core: 1024 words available. Endpoint 1 is
 * NKRO interrupt IN; endpoint 2 is bidirectional MIDI bulk, up to 512 bytes. */
#define USBD2_RX_SIZE 256
#define USBD2_EP0_TX_SIZE 32
#define USBD2_EP1_TX_SIZE 16
#define USBD2_EP2_TX_SIZE 256
#define USBD2_EP3_TX_SIZE 16
#define USBD2_EP4_TX_SIZE 16
#define USBD2_EP5_TX_SIZE 16
#define USBD2_EP6_TX_SIZE 16
#define USBD2_EP7_TX_SIZE 16
/* The vendor driver compiles both FIFO branches even when only OTG2 is used. */
#define USBD_RX_SIZE 128
#define USBD_EP0_TX_SIZE 32
#define USBD_EP1_TX_SIZE 16
#define USBD_EP2_TX_SIZE 64
#define USBD_EP3_TX_SIZE 16
#define USBD_EP4_TX_SIZE 16
#define USBD_EP5_TX_SIZE 16
#define USBD_EP6_TX_SIZE 16
#define USBD_EP7_TX_SIZE 16
void usb_delay_ms(uint32_t ms);
void usb_delay_us(uint32_t us);
#endif
