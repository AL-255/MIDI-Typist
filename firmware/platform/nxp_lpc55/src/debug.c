#include "debug.h"

#include <string.h>

#include "usb_composite.h"
#include "midi_control.h"
#include "fsl_common.h"
#include "scan_stream.h"

#define DEBUG_RING_SIZE 1024u
#define DEBUG_USB_CHUNK   128u

static uint8_t s_ring[DEBUG_RING_SIZE];
static uint16_t s_head;
static uint16_t s_tail;

void debug_init(void)
{
    s_head = s_tail = 0u;
}

void debug_write_bytes(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i)
    {
        const uint16_t next = (uint16_t)((s_head + 1u) % DEBUG_RING_SIZE);
        if (next == s_tail)
        {
            break;
        }
        s_ring[s_head] = data[i];
        s_head = next;
    }
}

void debug_write(const char *message)
{
    debug_write_bytes((const uint8_t *)message, strlen(message));
}

void debug_write_hex16(const char *label, uint16_t value)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t line[32];
    size_t length = 0u;
    while ((*label != '\0') && (length < 24u))
    {
        line[length++] = (uint8_t)*label++;
    }
    line[length++] = '0';
    line[length++] = 'x';
    line[length++] = (uint8_t)hex[(value >> 12u) & 0x0fu];
    line[length++] = (uint8_t)hex[(value >> 8u) & 0x0fu];
    line[length++] = (uint8_t)hex[(value >> 4u) & 0x0fu];
    line[length++] = (uint8_t)hex[value & 0x0fu];
    line[length++] = '\r';
    line[length++] = '\n';
    debug_write_bytes(line, length);
}

void debug_usb_configured(void) { s_tail=s_head; }

void debug_service(void)
{
    midi_control_service();
    if(!midi_control_ready()) { s_tail=s_head;return; }
    if(scan_stream_service())return;
    uint8_t packet[DEBUG_USB_CHUNK];
    unsigned length=0,tail=s_tail;
    while(tail!=s_head && length<sizeof(packet)) {
        packet[length++]=s_ring[tail];tail=(tail+1u)%DEBUG_RING_SIZE;
    }
    if(length && midi_control_publish(MT_LOG,packet,length))s_tail=tail;
}
