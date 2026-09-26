#ifndef MIDI_TYPIST_M1_USB_HAL_H
#define MIDI_TYPIST_M1_USB_HAL_H
#include <stdbool.h>
typedef enum {
    M1_USB_HW_OK, M1_USB_HW_CONTEXT, M1_USB_HW_BUSY, M1_USB_HW_CLOCK,
    M1_USB_HW_EXTERNAL, M1_USB_HW_PLLU, M1_USB_HW_DELAY, M1_USB_HW_CORE
} m1_usb_hw_result_t;
/* Foreground privileged owner, after m1_clock_init. Start requires explicit
 * quiescence, stopped SysTick/scan/LED/radio and disabled USB IRQs. It performs
 * the SDK's blocking startup, preserving PRIMASK and DWT/trace configuration.
 * PC13 must be configured as the external-power input and remain low.
 * Running means initialized/attached, NOT enumerated; use m1_usb_ready(). */
m1_usb_hw_result_t m1_usb_hw_start(bool platform_quiescent);
/* Detach and reset the owned PIO core before releasing class buffers. This is
 * an abort, not host acknowledgement or the board's low-power PHY sequence. */
m1_usb_hw_result_t m1_usb_hw_stop(void);
bool m1_usb_hw_running(void);
/* Route OTGHS_IRQn here from the platform vector table. No vector installed
 * by this library. Foreground power policy must stop the core on cable loss. */
void m1_usb_hw_irq(void);
#endif
