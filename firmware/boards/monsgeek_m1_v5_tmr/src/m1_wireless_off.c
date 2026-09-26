/* USB-only build: Bluetooth and 2.4 GHz are compiled out.
 *
 * This module replaces the radio HAL and the peer scheduler when
 * MT_M1_WIRELESS is 0. No radio code is linked into such an image, so there is
 * no SPI3 transfer, no pairing request, no peer report and no peer battery
 * metadata. It answers the same public API as "this device has no wireless
 * hardware": the foreground owner still parks, blanks, sleeps and restores,
 * but the sleep handoff has no peer transaction to complete and no link to
 * retain, and every wireless transport selection is refused.
 *
 * These answers must not be confused with a working wireless stack: any code
 * path that would report a wireless host, a delivered report or a peer sleep is
 * false here, and m1_wireless_supported() tells the host which build this is.
 */
#include "m1_wireless.h"
#include "m1_radio.h"

static uint8_t sleep_sent;
bool m1_wireless_supported(void) { return false; }

/* Radio HAL: nothing to initialize, never busy, never a transfer. Healthy is
 * true because there is no radio fault to report; stopping or quiescing it is
 * not an error and must not latch a terminal state. */
bool m1_radio_init(uint32_t now_us) { (void)now_us;return true; }
void m1_radio_service(uint32_t now_us) { (void)now_us; }
bool m1_radio_bus_idle(void) { return true; }
bool m1_radio_ready(void) { return true; }
bool m1_radio_healthy(void) { return true; }
uint32_t m1_radio_errors(void) { return 0; }
bool m1_radio_data_pending(void) { return false; }
bool m1_radio_exchange(const m1_radio_packet_t *packet,uint32_t now_us)
{ (void)packet;(void)now_us;return false; }
bool m1_radio_take(uint8_t *out,size_t capacity,size_t *length)
{ (void)out;(void)capacity;(void)length;return false; }
void m1_radio_stop(void) {}
bool m1_radio_quiesce(bool peer_asleep) { (void)peer_asleep;return true; }

/* Scheduler: USB is the only local transport. Init accepts USB (there is no
 * link to establish) and refuses every wireless mode, so boot and the source
 * owner cannot start this build on a radio transport. */
bool m1_wireless_init(m1_transport_t mode,bool previous_host_released,uint32_t now_us)
{ (void)previous_host_released;(void)now_us;return mode==M1_TRANSPORT_USB; }
void m1_wireless_service(uint32_t now_us) { (void)now_us; }
void m1_wireless_stop(void) { sleep_sent=0; }
bool m1_wireless_healthy(void) { return true; }
bool m1_wireless_mode(m1_transport_t *mode) { (void)mode;return false; }
bool m1_wireless_ready(void) { return false; }
bool m1_wireless_offer(const keyboard_report_t *report) { (void)report;return false; }
bool m1_wireless_consumer(uint16_t usage) { (void)usage;return false; }
bool m1_wireless_local_idle(void) { return true; }
bool m1_wireless_selected(m1_transport_t mode) { return mode==M1_TRANSPORT_USB; }
bool m1_wireless_switch_ready(void) { return true; }
bool m1_wireless_select(m1_transport_t mode,uint32_t now_us)
{ (void)now_us;return mode==M1_TRANSPORT_USB; }
bool m1_wireless_request_pair(bool host_released,uint32_t now_us)
{ (void)host_released;(void)now_us;return false; }
bool m1_wireless_pair_complete(void) { return false; }
bool m1_wireless_pairing(void) { return false; }
uint32_t m1_wireless_reports_sent(void) { return 0; }
uint32_t m1_wireless_errors(void) { return 0; }
uint32_t m1_wireless_fault_detail(void) { return 0; }
bool m1_wireless_status(m1_radio_status_t *out) { (void)out;return false; }
bool m1_wireless_battery(const m1_battery_t *battery) { (void)battery;return false; }
bool m1_wireless_battery_sent(uint8_t *percent) { (void)percent;return false; }
/* The sleep handoff has no peer: the exact requested command is reported as
 * locally completed so the owner proceeds to its GPIO/rail/RTC stages. This is
 * a local completion, never a claim that a peer slept. */
bool m1_wireless_request_sleep(uint8_t command,bool host_released)
{ (void)host_released;sleep_sent=command;return true; }
bool m1_wireless_cancel_sleep(void) { return true; }
uint8_t m1_wireless_sleep_sent(void) { return sleep_sent; }
bool m1_wireless_resume_retained(bool platform_restored,uint32_t now_us)
{ (void)platform_restored;(void)now_us;return true; }
