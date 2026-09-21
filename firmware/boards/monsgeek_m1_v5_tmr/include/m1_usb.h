#ifndef MIDI_TYPIST_M1_USB_H
#define MIDI_TYPIST_M1_USB_H
#include <stdbool.h>
#include <stddef.h>
#include "keyboard.h"
#include "usb_core.h"
enum { M1_USB_HID_IN=0x81, M1_USB_MIDI_IN=0x82, M1_USB_MIDI_OUT=2,
       M1_USB_FS_PACKET=64, M1_USB_HS_PACKET=512, M1_USB_INTERFACES=3 };
extern usbd_class_handler m1_usb_class;
extern usbd_desc_handler m1_usb_descriptors;
/* Before SDK initialization, with USB IRQs disabled and previous core stopped.
 * No clock, GPIO, PHY, update/flash or radio operations in this class layer. */
void m1_usb_bind(usbd_core_type *device);
bool m1_usb_ready(void);
bool m1_usb_drained(void); /* no local IN pending; inspect epoch for aborted work */
/* Copy on acceptance. Unchanged HID state is accepted without requeueing;
 * the class implements host SET_IDLE instead of forwarding app heartbeats. */
bool m1_usb_hid_send(const keyboard_report_t *report);
bool m1_usb_midi_send(const uint8_t *events,uint32_t size);
/* Foreground consume/rearm. An unread OUT packet NAKs subsequent host traffic.
 * Zero means no packet (or insufficient capacity); never silently truncates. */
unsigned m1_usb_midi_take(uint8_t *events,unsigned capacity);
/* Caller must reset SysEx/session and invalidate application output on change,
 * including halt/clear-halt and wake. Handle this before offering more output. */
uint32_t m1_usb_generation(void);
uint32_t m1_usb_errors(void);
uint8_t m1_usb_leds(void);
/* Descriptor access is also used by the checked setup-request adapter. */
usbd_desc_t *m1_usb_report_descriptor(void);
usbd_desc_t *m1_usb_hid_descriptor(void);
usbd_desc_t *m1_usb_other_descriptor(bool high_speed);
#endif
