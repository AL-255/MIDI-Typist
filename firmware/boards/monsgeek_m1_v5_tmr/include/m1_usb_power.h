#ifndef MIDI_TYPIST_M1_USB_POWER_H
#define MIDI_TYPIST_M1_USB_POWER_H
#include <stdbool.h>
typedef enum {
    M1_USB_POWER_OK, M1_USB_POWER_CONTEXT, M1_USB_POWER_BUSY,
    M1_USB_POWER_CLOCK, M1_USB_POWER_EXTERNAL, M1_USB_POWER_TIMEOUT
} m1_usb_power_result_t;
/* Foreground battery-sleep/cold-start helper, not USB bus suspend. The caller
 * must drain reports, stop its USB stack and periodic acquisition/output, and
 * configure PC13 as the board's external-power input. No live USB connection
 * is shut down by this API. The official SDK power-down routine is reused. */
m1_usb_power_result_t m1_usb_power_down(bool platform_quiescent);
/* Success is latched only after suspend is observed after the SDK gates the PHY.
 * Rechecks power/PHY state. It is not a measured current-consumption claim. */
bool m1_usb_power_ready(void);
/* Call BEFORE reinitializing the USB core/PHY on cable insertion or resume.
 * Restoration belongs to the USB driver; this helper does not enumerate. */
void m1_usb_power_invalidate(void);
#endif
