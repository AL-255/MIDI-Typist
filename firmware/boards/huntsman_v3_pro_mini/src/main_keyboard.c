#include "board.h"
#include "debug.h"
#include "midi_control.h"
#include "keyboard_live.h"
#include "usb_composite.h"

/* The complete application starts scanning after USB configuration and arms
 * output after neutral samples. SysEx control is independent of performance. */

int main(void)
{
    board_init();
    board_watchdog_refresh();
    debug_init();
    keyboard_live_init();
    usb_composite_init();
    keyboard_live_control_bind();
    debug_write("MIDI-Typist SysEx control ready\r\n");
    for (;;)
    {
        usb_composite_service();
        keyboard_live_service();
        debug_service();
        board_watchdog_refresh();
        __WFI();
    }
}
