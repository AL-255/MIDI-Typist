#ifndef HUNTSMAN_USB_COMPOSITE_H
#define HUNTSMAN_USB_COMPOSITE_H

#include <stdbool.h>
#include <stdint.h>

#include "keyboard.h"
#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"

void usb_composite_init(void);
void usb_composite_service(void);
bool usb_keyboard_send(const keyboard_report_t *report);
uint8_t usb_keyboard_leds(void);
bool usb_midi_send(uint8_t cable_and_cin, uint8_t status, uint8_t data1, uint8_t data2);
bool usb_midi_write_events(const uint8_t *data, uint32_t length);
bool usb_composite_ready(void);
usb_device_handle usb_composite_device_handle(void);

#endif
