/* Foreground integration audit. USB executes its real class/SDK; acquisition,
 * lights and battery completion are explicit scripted ports, not physical IO. */
#include "m1_live.h"
#include "m1_hal.h"
#include "m1_lighting.h"
#include "m1_battery_hal.h"
#include "m1_wireless.h"
#include "scan_stream.h"
#include <string.h>
static uint16_t frame[M1_KEY_COUNT];
static uint32_t sequence;
static bool pending,periodic=true,led_ready=true;
static uint8_t rgb[M1_LED_BYTES];
static unsigned light_frames;
static m1_battery_t battery;
bool __wrap_m1_hal_periodic_active(void) { return periodic; }
bool __wrap_m1_hal_frame(uint16_t *out,uint32_t *seq)
{
    if(!pending)return false;
    memcpy(out,frame,sizeof(frame));*seq=sequence;pending=false;return true;
}
void __wrap_m1_hal_service(uint32_t now) { (void)now; }
uint32_t __wrap_m1_hal_errors(void) { return 0; }
void __wrap_m1_battery_hal_service(uint32_t now) { (void)now; }
const m1_battery_t *__wrap_m1_battery_hal_status(void) { return &battery; }
void __wrap_m1_lighting_service(uint32_t now) { (void)now; }
bool __wrap_m1_lighting_ready(void) { return led_ready; }
bool __wrap_m1_lighting_healthy(void) { return true; }
uint32_t __wrap_m1_lighting_errors(void) { return 0; }
bool __wrap_m1_lighting_offer(const uint8_t *data,size_t size,uint32_t now)
{
    (void)now;
    if(!led_ready || size!=sizeof(rgb))return false;
    memcpy(rgb,data,size);++light_frames;return true;
}
void m1_test_live_frame(const uint16_t *data,uint32_t seq)
{ memcpy(frame,data,sizeof(frame));sequence=seq;pending=true; }
void m1_test_live_periodic(unsigned value) { periodic=value!=0; }
void m1_test_live_led(unsigned value) { led_ready=value!=0; }
uintptr_t m1_test_live_get(unsigned field)
{ return field==0?(uintptr_t)rgb:field==1?light_frames:pending; }
static bool host_drained,selection_ready;
static unsigned requested,select_calls;
static bool drained(void *context) { (void)context;return host_drained; }
static bool select_transport(void *context,m1_transport_t target)
{ (void)context;requested=target;++select_calls;return selection_ready; }
uintptr_t m1_test_live_transports(void)
{ static const m1_transport_ops_t ops={drained,select_transport,NULL};return (uintptr_t)&ops; }
void m1_test_live_transport_gate(unsigned drained,unsigned ready)
{ host_drained=drained!=0;selection_ready=ready!=0; }
unsigned m1_test_live_selection(unsigned field) { return field?select_calls:requested; }
void m1_test_live_battery(uint8_t percent,bool valid)
{ battery=(m1_battery_t){.percent=percent,.valid=valid,.source_known=valid}; }
__attribute__((used,section(".test_exports")))
const void *const m1_live_test_exports[]={
    m1_live_init,m1_live_service,m1_live_stop,m1_live_scan_losses,m1_live_transport,m1_live_transport_fault,
    m1_test_live_frame,m1_test_live_periodic,m1_test_live_led,m1_test_live_get,
    scan_stream_lost,scan_stream_dropped,
    m1_radio_init,m1_radio_service,m1_radio_healthy,m1_radio_ready,
    m1_wireless_init,m1_wireless_stop,m1_wireless_ready,m1_wireless_healthy,m1_wireless_local_idle,
    m1_wireless_reports_sent,m1_wireless_mode,m1_wireless_service,
    m1_test_live_transports,m1_test_live_transport_gate,m1_test_live_selection,m1_test_live_battery
};
