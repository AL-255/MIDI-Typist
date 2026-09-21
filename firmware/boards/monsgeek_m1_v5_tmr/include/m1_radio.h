#ifndef MIDI_TYPIST_M1_RADIO_H
#define MIDI_TYPIST_M1_RADIO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SPI3 peer protocol, independent of USB/SysEx framing. */
#define M1_RADIO_BUFFER_BYTES 80u
#define M1_RADIO_PAYLOAD_MAX 65u
#define M1_RADIO_POLL_BYTES 68u
#define M1_RADIO_SPI_DIV 16u
enum {
    M1_RADIO_POLL=0x09, M1_RADIO_REPORT=0x81, M1_RADIO_BATTERY=0x90,
    M1_RADIO_STATUS_REQUEST=0x92, M1_RADIO_MODE=0x93, M1_RADIO_CONTROL=0x94,
    M1_RADIO_REPLY_STATUS=0x10
};
typedef struct {
    uint8_t bytes[M1_RADIO_BUFFER_BYTES];
    uint8_t size; /* whole DMA transaction, including zero padding */
} m1_radio_packet_t;
typedef struct {
    uint8_t kind,length; /* payload length AFTER the kind byte */
    uint8_t data[M1_RADIO_PAYLOAD_MAX-1u];
} m1_radio_reply_t;
typedef struct { uint8_t flags,state,mode; } m1_radio_status_t;
/* Only reviewed runtime opcodes are emitted, not vendor-command forwarding or
 * peer firmware-update commands. Payload semantics belong to the controller.
 * Invalid inputs leave output unchanged. Padding is always zeroed. */
bool m1_radio_encode(m1_radio_packet_t *out,uint8_t opcode,const uint8_t *payload,size_t length);
void m1_radio_make_poll(m1_radio_packet_t *out);
bool m1_radio_decode(const uint8_t *bytes,size_t transferred,m1_radio_reply_t *out);
bool m1_radio_status(const m1_radio_reply_t *reply,m1_radio_status_t *out);

/* Foreground-only HAL; SDK SPI3 + DMA1 channels 2(TX)/3(RX). Caller establishes
 * clocks and peer power. Init is nonblocking: PA15 low for the startup pulse,
 * then high/ready. Reinitialization requires stop or a fault first. */
bool m1_radio_init(uint32_t now_us);
void m1_radio_service(uint32_t now_us);
bool m1_radio_ready(void);
bool m1_radio_healthy(void);
uint32_t m1_radio_errors(void);
/* PD2 is active-low data-ready; false while the HAL cannot start a poll. */
bool m1_radio_data_pending(void);
/* Copy-on-accept. No second transfer until both DMA channels and the SPI
 * shifter finish AND the caller consumes the received transaction. */
bool m1_radio_exchange(const m1_radio_packet_t *packet,uint32_t now_us);
bool m1_radio_take(uint8_t *out,size_t capacity,size_t *length);
/* Abort an outstanding transaction, disable owned DMA/SPI and hold PA15 high.
 * This is not a radio sleep command and does not force PD2 to an output. */
void m1_radio_stop(void);
/* Match the reference's power-down pin pattern ONLY after the higher layer
 * proves peer sleep. Busy/unconsumed transfers must finish first. No retries. */
bool m1_radio_quiesce(bool peer_asleep);
#endif
