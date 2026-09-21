#ifndef MIDI_TYPIST_M1_WIRELESS_H
#define MIDI_TYPIST_M1_WIRELESS_H
#include "m1_radio_keyboard.h"
#include "m1_controls.h"
/* Single foreground owner of the radio HAL. Init requires initialized, idle
 * SPI3 and explicit permission from the outer coordinator: previous host
 * releases and peer power are its responsibility. Never use local_idle as a
 * host-delivery acknowledgement. No pairing/sleep/boot commands are emitted. */
bool m1_wireless_init(m1_transport_t mode,bool previous_host_released,uint32_t now_us);
void m1_wireless_service(uint32_t now_us);
void m1_wireless_stop(void);
bool m1_wireless_healthy(void);
/* True only for a fresh matching mode/state-3 status received by polling. This
 * is the reference's report-eligibility state, NOT proof of host delivery. */
bool m1_wireless_ready(void);
bool m1_wireless_offer(const keyboard_report_t *report);
bool m1_wireless_local_idle(void);
uint32_t m1_wireless_reports_sent(void);
uint32_t m1_wireless_errors(void);
bool m1_wireless_status(m1_radio_status_t *out);
#endif
