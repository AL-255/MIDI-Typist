/* Offline ARM audit fixture, not a firmware entry point or flashable image. */
#include "usb_core.h"
volatile uint32_t probe_milliseconds;
uint32_t at32_board_millis(void) { return probe_milliseconds; }
void probe_bind(otg_core_type *core,usbd_class_handler *handlers,usbd_desc_handler *descriptors)
{
    core->dev.class_handler=handlers;core->dev.desc_handler=descriptors;
    core->dev.speed=USB_HIGH_SPEED;
    core->dev.conn_state=USB_CONN_STATE_CONFIGURED;
}
void probe_speed(usbd_core_type *dev,unsigned high)
{ dev->speed=high?USB_HIGH_SPEED:USB_FULL_SPEED; }
void probe_connected(usbd_core_type *dev,unsigned connected)
{ dev->conn_state=connected?USB_CONN_STATE_CONFIGURED:USB_CONN_STATE_DEFAULT; }
usb_sts_type __wrap_usbd_device_request(usbd_core_type *dev);
usb_sts_type __wrap_usbd_endpoint_request(usbd_core_type *dev);
usb_sts_type probe_request(usbd_core_type *dev,const usb_setup_type *setup,unsigned endpoint)
{
    dev->setup=*setup;
    return endpoint?__wrap_usbd_endpoint_request(dev):__wrap_usbd_device_request(dev);
}
