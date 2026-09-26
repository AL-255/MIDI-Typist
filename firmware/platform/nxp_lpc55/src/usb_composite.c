#include "usb_composite.h"

#include <string.h>

#include "board.h"
#include "fsl_common.h"
#include "debug.h"
#include "updater_protocol.h"
#include "usb_descriptors.h"
#include "usb_errata.h"
#include "midi_control.h"
#include "usb_device_config.h"
#include "usb_device_hid.h"
#include "usb_device_dci.h"
#include "keyboard_live.h"

#define USB_CONTROLLER_ID ((uint8_t)kUSB_ControllerLpcIp3511Hs0)
#define USB_BUFFER __attribute__((section(".usb_sram"), aligned(64)))

static usb_device_handle s_device;
static volatile bool s_attached;
static volatile bool s_keyboard_busy;
static volatile uint8_t s_keyboard_leds;
static volatile bool s_midi_busy;
static volatile bool s_bootloader_pending;
static volatile bool s_bootloader_status_complete;
static volatile uint32_t s_bootloader_requested_at;
static usb_device_class_config_struct_t s_classConfig[2];

USB_BUFFER static keyboard_report_t s_keyboard_report;
USB_BUFFER static uint8_t s_keyboard_led_report[1];
USB_BUFFER static uint8_t s_updater_request[UPDATER_FRAME_SIZE];
USB_BUFFER static uint8_t s_updater_response[UPDATER_FRAME_SIZE];
USB_BUFFER static uint8_t s_midi_tx[USB_HS_BULK_PACKET];
USB_BUFFER static uint8_t s_midi_rx[USB_HS_BULK_PACKET];

static usb_status_t keyboard_callback(class_handle_t handle, uint32_t event, void *param)
{
    usb_device_hid_report_struct_t *report = param;
    switch (event)
    {
        case kUSB_DeviceHidEventSendResponse:
            s_keyboard_busy = false;
            return kStatus_USB_Success;
        case kUSB_DeviceHidEventGetReport:
            if(report->reportId != 0u)return kStatus_USB_InvalidRequest;
            if(report->reportType == USB_DEVICE_HID_REQUEST_GET_REPORT_TYPE_INPUT) {
                report->reportBuffer = (uint8_t *)&s_keyboard_report;
                report->reportLength = sizeof(s_keyboard_report);
                return kStatus_USB_Success;
            }
            if(report->reportType == USB_DEVICE_HID_REQUEST_GET_REPORT_TYPE_OUPUT) {
                s_keyboard_led_report[0] = s_keyboard_leds;
                report->reportBuffer = s_keyboard_led_report;
                report->reportLength = sizeof(s_keyboard_led_report);
                return kStatus_USB_Success;
            }
            return kStatus_USB_InvalidRequest;
        case kUSB_DeviceHidEventRequestReportBuffer:
            if(report->reportId != 0u || report->reportType != USB_DEVICE_HID_REQUEST_GET_REPORT_TYPE_OUPUT ||
               report->reportLength != sizeof(s_keyboard_led_report))return kStatus_USB_InvalidRequest;
            report->reportBuffer = s_keyboard_led_report;
            return kStatus_USB_Success;
        case kUSB_DeviceHidEventSetReport:
            if(report->reportId != 0u || report->reportType != USB_DEVICE_HID_REQUEST_GET_REPORT_TYPE_OUPUT ||
               report->reportLength != sizeof(s_keyboard_led_report))return kStatus_USB_InvalidRequest;
            s_keyboard_leds = report->reportBuffer[0] & 0x1fu;
            return kStatus_USB_Success;
        case kUSB_DeviceHidEventSetIdle:
        case kUSB_DeviceHidEventGetIdle:
            return kStatus_USB_Success;
        case kUSB_DeviceHidEventSetProtocol:
        case kUSB_DeviceHidEventGetProtocol:
            /* Report-only NKRO HID; no eight-byte boot protocol advertised. */
            return kStatus_USB_InvalidRequest;
        default:
            return kStatus_USB_InvalidRequest;
    }
}

static usb_status_t updater_callback(class_handle_t handle, uint32_t event, void *param)
{
    usb_device_hid_report_struct_t *report = param;
    switch (event)
    {
        case kUSB_DeviceHidEventRequestReportBuffer:
            if ((report->reportType == USB_DEVICE_HID_REQUEST_GET_REPORT_TYPE_FEATURE) &&
                (report->reportLength <= sizeof(s_updater_request)))
            {
                report->reportBuffer = s_updater_request;
                return kStatus_USB_Success;
            }
            return kStatus_USB_InvalidRequest;

        case kUSB_DeviceHidEventSetReport:
            if ((report->reportType == USB_DEVICE_HID_REQUEST_GET_REPORT_TYPE_FEATURE) &&
                (report->reportLength == sizeof(s_updater_request)))
            {
                const updater_action_t action = updater_protocol_handle(
                    report->reportBuffer, report->reportLength, s_updater_response);
                if (action == kUpdaterEnterBootloader)
                {
                    s_bootloader_pending = true;
                    s_bootloader_status_complete = false;
                }
                return (action == kUpdaterNoAction) ? kStatus_USB_InvalidRequest : kStatus_USB_Success;
            }
            return kStatus_USB_InvalidRequest;

        case kUSB_DeviceHidEventGetReport:
            if (report->reportType == USB_DEVICE_HID_REQUEST_GET_REPORT_TYPE_FEATURE)
            {
                report->reportBuffer = s_updater_response;
                report->reportLength = sizeof(s_updater_response);
                return kStatus_USB_Success;
            }
            return kStatus_USB_InvalidRequest;
        default:
            return kStatus_USB_InvalidRequest;
    }
}

static usb_status_t midi_in_callback(usb_device_handle handle,
                                    usb_device_endpoint_callback_message_struct_t *message, void *param)
{
    s_midi_busy = false;
    return kStatus_USB_Success;
}

static usb_status_t midi_out_callback(usb_device_handle handle,
                                     usb_device_endpoint_callback_message_struct_t *message, void *param)
{
    if (!s_attached || message->length == USB_CANCELLED_TRANSFER_LENGTH)
        return kStatus_USB_Success;
    if(message->length<=sizeof(s_midi_rx)) midi_control_receive_usb(message->buffer,message->length);
    return USB_DeviceRecvRequest(handle, USB_MIDI_ENDPOINT, s_midi_rx,
                                 g_midiEndpoints[0].maxPacketSize);
}

static usb_status_t midi_endpoints_init(usb_device_handle handle)
{
    /* NXP's PCM AudioStreaming class only accepts subclass 2 and iso IN.
     * MIDIStreaming is subclass 3 with bulk endpoints: use the vendor DCI. */
    for (unsigned i = 0; i < 2; ++i)
    {
        usb_device_endpoint_init_struct_t endpoint = {
            .endpointAddress = g_midiEndpoints[i].endpointAddress,
            .transferType = USB_ENDPOINT_BULK,
            .maxPacketSize = g_midiEndpoints[i].maxPacketSize,
            .zlt = 0u,
            .interval = 0u,
        };
        usb_device_endpoint_callback_struct_t callback = {
            .callbackFn = i ? midi_in_callback : midi_out_callback,
            .callbackParam = NULL,
        };
        const usb_status_t status = USB_DeviceInitEndpoint(handle, &endpoint, &callback);
        if (status != kStatus_USB_Success)
            return status;
    }
    return USB_DeviceRecvRequest(handle, USB_MIDI_ENDPOINT, s_midi_rx,
                                 g_midiEndpoints[0].maxPacketSize);
}

static usb_status_t device_callback(usb_device_handle handle, uint32_t event, void *param)
{
    switch (event)
    {
        case kUSB_DeviceEventBusReset:
            midi_control_usb_reset();
            keyboard_live_usb_reset();
        {
            uint8_t speed = USB_SPEED_FULL;
            s_attached = false;
            s_keyboard_busy = s_midi_busy = false;
            s_keyboard_leds = 0u;
            s_bootloader_pending = s_bootloader_status_complete = false;
            usb_errata_bus_reset();
            if (USB_DeviceClassGetSpeed(USB_CONTROLLER_ID, &speed) == kStatus_USB_Success)
            {
                usb_descriptors_set_speed(speed);
            }
            return kStatus_USB_Success;
        }
        case kUSB_DeviceEventSetConfiguration:
            midi_control_usb_reset();
            s_attached = false;
            s_keyboard_leds = 0u;
            s_midi_busy = false;
            (void)USB_DeviceDeinitEndpoint(handle, USB_MIDI_ENDPOINT);
            (void)USB_DeviceDeinitEndpoint(handle, USB_ENDPOINT_IN | USB_MIDI_ENDPOINT);
            if ((param != NULL) && (*(uint8_t *)param == 1u))
            {
                s_attached = true;
                if (midi_endpoints_init(handle) != kStatus_USB_Success)
                {
                    s_attached = false;
                    return kStatus_USB_Error;
                }
                debug_usb_configured();
                return kStatus_USB_Success;
            }
            s_attached = false;
            return kStatus_USB_Success;
        case kUSB_DeviceEventGetConfiguration:
            if (param == NULL)
                return kStatus_USB_InvalidRequest;
            *(uint8_t *)param = s_attached ? 1u : 0u;
            return kStatus_USB_Success;
        case kUSB_DeviceEventGetInterface:
        case kUSB_DeviceEventSetInterface:
            if (param == NULL || !s_attached ||
                (*(uint16_t *)param >> 8u) >= USB_IFACE_COUNT ||
                (*(uint16_t *)param & 0xffu) != 0u)
                return kStatus_USB_InvalidRequest;
            /* Every exposed interface has only alternate setting zero. */
            return kStatus_USB_Success;
        default:
            return usb_descriptors_handle_event(event, param);
    }
}

static usb_device_class_config_struct_t s_classConfig[2] = {
    {keyboard_callback, NULL, &g_keyboardClass},
    {updater_callback, NULL, &g_updaterClass},
};

static usb_device_class_config_list_struct_t s_config_list = {
    .config = s_classConfig,
    .deviceCallback = device_callback,
    .count = (uint8_t)(sizeof(s_classConfig) / sizeof(s_classConfig[0])),
};

void USB1_IRQHandler(void)
{
    USB_DeviceLpcIp3511IsrFunction(s_device);
    USBHSD->INTEN |= USBHSD_INTEN_FRAME_INT_EN_MASK;
}

usb_status_t __real_USB_DeviceNotificationTrigger(void *handle, void *message);

usb_status_t __wrap_USB_DeviceNotificationTrigger(void *handle, void *message)
{
    const usb_device_callback_message_struct_t *event = message;
    /* NXP IP3511 reports EP0 IN as code 0x80, not endpoint-list index 1.
     * A completed zero-length IN after our SET_REPORT data is its status ACK.
     * A new SETUP before that ACK aborts the old control transfer. */
    const bool status_complete = event != NULL && event->code == 0x80u &&
        event->isSetup == 0u && event->length == 0u && s_bootloader_pending &&
        !s_bootloader_status_complete;
    if (event != NULL && event->isSetup != 0u && !s_bootloader_status_complete)
        s_bootloader_pending = false;
    const usb_status_t result = __real_USB_DeviceNotificationTrigger(handle, message);
    /* The vendor Chapter 9 callback leaves its return value InvalidRequest
     * on a status-only completion. The controller completion notification,
     * not that callback return, is the evidence that the ZLP was ACKed. */
    if (status_complete && s_bootloader_pending)
    {
        s_bootloader_requested_at = board_millis();
        s_bootloader_status_complete = true;
    }
    return result;
}

void usb_composite_init(void)
{
    static const midi_control_port_t control_port = {
        board_millis, usb_composite_ready, usb_midi_write_events,
        DisableGlobalIRQ, EnableGlobalIRQ
    };
    usb_errata_init();
    board_usb_clock_init();
    memset(&s_keyboard_report, 0, sizeof(s_keyboard_report));
    s_keyboard_leds = 0u;
    memset(s_updater_response, 0, sizeof(s_updater_response));
    (void)midi_control_init(&control_port);
    if (USB_DeviceClassInit(USB_CONTROLLER_ID, &s_config_list, &s_device) != kStatus_USB_Success)
    {
        s_device = NULL;
        return;
    }
    board_delay_ms(20u);
    board_usb_isr_enable();
    (void)USB_DeviceRun(s_device);
}

void usb_composite_service(void)
{
    if (s_bootloader_pending && s_bootloader_status_complete &&
        ((uint32_t)(board_millis() - s_bootloader_requested_at) >= 20u))
    {
        board_enter_bootloader();
    }
}

bool usb_keyboard_send(const keyboard_report_t *report)
{
    const uint32_t irq = DisableGlobalIRQ();
    if (!s_attached || s_keyboard_busy)
    {
        EnableGlobalIRQ(irq);
        return false;
    }
    memcpy(&s_keyboard_report, report, sizeof(s_keyboard_report));
    s_keyboard_busy = true;
    const bool submitted = USB_DeviceHidSend(s_classConfig[0].classHandle, USB_KEYBOARD_ENDPOINT,
                                        (uint8_t *)&s_keyboard_report,
                                        sizeof(s_keyboard_report)) == kStatus_USB_Success;
    if (!submitted)
        s_keyboard_busy = false;
    EnableGlobalIRQ(irq);
    return submitted;
}

uint8_t usb_keyboard_leds(void) { return s_attached ? s_keyboard_leds : 0u; }

bool usb_midi_send(uint8_t cable_and_cin, uint8_t status, uint8_t data1, uint8_t data2)
{
    const uint32_t irq = DisableGlobalIRQ();
    if (!s_attached || s_midi_busy)
    {
        EnableGlobalIRQ(irq);
        return false;
    }
    s_midi_tx[0] = cable_and_cin;
    s_midi_tx[1] = status;
    s_midi_tx[2] = data1;
    s_midi_tx[3] = data2;
    s_midi_busy = true;
    const bool submitted = USB_DeviceSendRequest(s_device, USB_MIDI_ENDPOINT,
                                      s_midi_tx, 4u) == kStatus_USB_Success;
    if (!submitted)
        s_midi_busy = false;
    EnableGlobalIRQ(irq);
    return submitted;
}

bool usb_midi_write_events(const uint8_t *data,uint32_t length)
{
    if(!data || !length || length%4 || length>sizeof(s_midi_tx))return false;
    uint32_t irq=DisableGlobalIRQ();
    if(!s_attached || s_midi_busy) { EnableGlobalIRQ(irq);return false; }
    memcpy(s_midi_tx,data,length);s_midi_busy=true;
    bool ok=USB_DeviceSendRequest(s_device,USB_MIDI_ENDPOINT,s_midi_tx,length)==kStatus_USB_Success;
    if(!ok)s_midi_busy=false;
    EnableGlobalIRQ(irq);return ok;
}

bool usb_composite_ready(void) { return s_attached; }

usb_device_handle usb_composite_device_handle(void)
{
    return s_device;
}
