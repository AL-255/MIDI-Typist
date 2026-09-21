#ifndef MIDI_TYPIST_M1_WIRELESS_H
#define MIDI_TYPIST_M1_WIRELESS_H
#include "m1_radio_keyboard.h"
#include "m1_controls.h"
/* Single foreground owner of the radio HAL. Init requires initialized, idle
 * SPI3 and explicit permission from the outer coordinator: previous host
 * releases and peer power are its responsibility. Never use local_idle as a
 * host-delivery acknowledgement. No pairing or boot commands are emitted. */
bool m1_wireless_init(m1_transport_t mode,bool previous_host_released,uint32_t now_us);
void m1_wireless_service(uint32_t now_us);
void m1_wireless_stop(void);
bool m1_wireless_healthy(void);
/* Configured mode of a healthy scheduler, including while awaiting the peer.
 * This is not confirmation of the received mode or link readiness. */
bool m1_wireless_mode(m1_transport_t *mode);
/* True only for a fresh matching mode/state-3 status received by polling. This
 * is the reference's report-eligibility state, NOT proof of host delivery. */
bool m1_wireless_ready(void);
bool m1_wireless_offer(const keyboard_report_t *report);
bool m1_wireless_local_idle(void);
uint32_t m1_wireless_reports_sent(void);
uint32_t m1_wireless_errors(void);
bool m1_wireless_status(m1_radio_status_t *out);
/* Copy the current filtered percentage. NULL/invalid input cancels unsent
 * battery metadata, never an already-started DMA. Call when the battery HAL
 * updates or invalidates its sample. Charger-pin polarity is not encoded. */
bool m1_wireless_battery(const m1_battery_t *battery);
/* Last locally completed percentage, not necessarily the latest offered one. */
bool m1_wireless_battery_sent(uint8_t *percent);
/* Explicit power handoff: 3 for sleep, 5 for BT retention. Caller must prove
 * host releases; the scheduler separately requires neutral completed reports.
 * Command 3 also admits a never-linked neutral startup for critical protection.
 * Accepted requests reject new key/battery offers and stop regular polling.
 * Cancellation succeeds only BEFORE the command enters the HAL. Afterward a
 * wake/restore sequence is required; stop/reinit is not proof of restoration. */
/* Completed retention (5) may be escalated to 3 without restarting traffic.
 * Cancelling that escalation before submission restores the quiet state 5. */
bool m1_wireless_request_sleep(uint8_t command,bool host_released);
bool m1_wireless_cancel_sleep(void);
/* Returns 0 until the exact control transaction has COMPLETED locally, then
 * its payload (3/5). May feed m1_power_radio_committed, never peer_asleep or
 * host_drained. No power rails, sleep GPIO pattern, system clocks or RTC are
 * changed by this handoff; the ordinary SPI HAL still drives chip select. */
uint8_t m1_wireless_sleep_sent(void);
#endif
