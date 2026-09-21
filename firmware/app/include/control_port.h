#ifndef MIDI_TYPIST_CONTROL_PORT_H
#define MIDI_TYPIST_CONTROL_PORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Monotonic milliseconds, wrapping modulo 2^32; callable from USB IRQ. */
uint32_t control_port_millis(void);
/* Mask the USB receive ISR, preserving/restoring the previous mask exactly. */
uint32_t control_port_lock(void);
void control_port_unlock(uint32_t state);
bool control_port_ready(void);
/* Main context, nonblocking: copy ALL events before returning true. On false,
 * consume none. Events are complete four-byte USB-MIDI packets. The platform
 * arbitrates this stream with performance MIDI; it must not retain this pointer. */
bool control_port_write(const uint8_t *events, size_t length);
#endif
