#include "control_port.h"
#include "board.h"
#include "usb_composite.h"
#include "fsl_common.h"

uint32_t control_port_millis(void) { return board_millis(); }
uint32_t control_port_lock(void) { return DisableGlobalIRQ(); }
void control_port_unlock(uint32_t state) { EnableGlobalIRQ(state); }
bool control_port_ready(void) { return usb_composite_ready(); }
bool control_port_write(const uint8_t *events, size_t length)
{
    return usb_midi_write_events(events, length);
}
