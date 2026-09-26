#ifndef MIDI_TYPIST_M1_WIRELESS_H
#define MIDI_TYPIST_M1_WIRELESS_H
#include "m1_radio_keyboard.h"
#include "m1_controls.h"
/* Bluetooth and 2.4 GHz are not qualified on this board: peer timing, pairing,
 * host delivery and the radio sleep handoff are unverified. The default build
 * compiles the radio HAL and scheduler out and links m1_wireless_off.c instead,
 * leaving a USB-only keyboard that cannot select a wireless transport.
 * Development and audit builds enable the feature with -DMT_M1_WIRELESS=1. */
#ifndef MT_M1_WIRELESS
#define MT_M1_WIRELESS 0
#endif
#if MT_M1_WIRELESS != 0 && MT_M1_WIRELESS != 1
#error "MT_M1_WIRELESS is a 0/1 build switch"
#endif
/* False when this artifact has no wireless feature at all: no code may then
 * advertise pairing or peer sleep, every wireless transport selection is
 * refused, and the GUI is told the build is USB-only. */
bool m1_wireless_supported(void);
/* Single foreground owner of the radio HAL. Init requires initialized, idle
 * SPI3 and explicit permission from the outer coordinator: previous host
 * releases and peer power are its responsibility. Never use local_idle as a
 * host-delivery acknowledgement. Pairing requires a separate explicit request;
 * boot/firmware-update commands are never emitted. */
bool m1_wireless_init(m1_transport_t mode,bool previous_host_released,uint32_t now_us);
void m1_wireless_service(uint32_t now_us);
void m1_wireless_stop(void);
bool m1_wireless_healthy(void);
/* Configured mode of a healthy scheduler, including while awaiting the peer.
 * This is not confirmation of the received mode or link readiness. */
bool m1_wireless_mode(m1_transport_t *mode);
/* True only for a fresh matching mode/state-3 status received by polling. This
 * is the reference's report-eligibility state, NOT proof of host delivery.
 * Same-mode offline/search states discard prior keyboard/consumer input and
 * remain healthy. The caller must invalidate held physical inputs on both
 * readiness edges and release consumer controls before accepting new input.
 * Returning state 3 sends a neutral keyboard baseline before new offers. */
bool m1_wireless_ready(void);
bool m1_wireless_offer(const keyboard_report_t *report);
/* Consumer usage (zero releases), copied into an independent pending slot.
 * Readiness/neutrality is enforced together with keyboard report ownership. */
bool m1_wireless_consumer(uint16_t usage);
bool m1_wireless_local_idle(void);
/* Mode selection does not require a connected wireless host. A fresh matching
 * mode reply confirms selection; state 3 separately gates keyboard reports. */
bool m1_wireless_selected(m1_transport_t mode);
/* Neutral local boundary, not host receipt: no outstanding transfer or held
 * committed/staged key. An unsent neutral baseline may be cancelled. */
bool m1_wireless_switch_ready(void);
/* Awake peer only. Keep SPI/rails initialized; cancel unsent neutral metadata,
 * send 0x93 and query status. Mode 6 parks wireless reporting for USB. */
bool m1_wireless_select(m1_transport_t mode,uint32_t now_us);
/* One explicit pairing request after mode selection and neutral completion.
 * Reject held/pending output or another control transaction. No automatic
 * repeat: completion requires finished DMA plus a subsequent matching status,
 * NOT connection to a host. State 3 separately permits keyboard reports. */
bool m1_wireless_request_pair(bool host_released,uint32_t now_us);
bool m1_wireless_pair_complete(void);
bool m1_wireless_pairing(void);
uint32_t m1_wireless_reports_sent(void);
uint32_t m1_wireless_errors(void);
/* Retained after a terminal fault/stop. Low byte: 1 pairing deadline,
 * 2 sleep deadline, 3 mode deadline, 5 SPI HAL,
 * 6 unsolicited mode, 7 unsupported state, 8 invalid pair request.
 * Remaining bytes: in-flight operation, last peer state, last peer mode. */
uint32_t m1_wireless_fault_detail(void);
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
/* Restore a completed BT-retention handoff without a GPIO reset pulse. Caller
 * has restored clocks/pins. Require a new mode/status handshake and neutral
 * baseline before reports; never reuse pre-sleep link readiness. */
bool m1_wireless_resume_retained(bool platform_restored,uint32_t now_us);
#endif
