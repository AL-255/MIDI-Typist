/* Foreground integration audit. USB executes its real class/SDK; acquisition,
 * lights and battery completion are explicit scripted ports, not physical IO. */
#include "m1_live.h"
#include "m1_hal.h"
#include "m1_encoder.h"
#include "m1_lighting.h"
#include "m1_battery_hal.h"
#include "m1_wireless.h"
#include "scan_stream.h"
#include "m1_storage.h"
#include "m1_transport.h"
#include <string.h>
static uint16_t frame[M1_KEY_COUNT];
static uint32_t sequence;
static uint32_t scan_errors;
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
uint32_t __wrap_m1_hal_errors(void) { return scan_errors; }
uint32_t __wrap_m1_hal_queue_state(void) { return 3u<<16 | 1u; }
void m1_test_live_scan_errors(uint32_t errors) { scan_errors=errors; }
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
{ static const m1_transport_ops_t ops={.drained=drained,.select=select_transport};return (uintptr_t)&ops; }
void m1_test_live_transport_gate(unsigned drained,unsigned ready)
{ host_drained=drained!=0;selection_ready=ready!=0; }
unsigned m1_test_live_selection(unsigned field) { return field?select_calls:requested; }
void m1_test_live_battery(uint8_t percent,bool valid)
{ battery=(m1_battery_t){.percent=percent,.valid=valid,.source_known=valid}; }
/* Owner safety and flash effects are scripted here; the separate storage
 * audit executes the actual SDK transaction and validates its guards. */
static uint8_t pages[2][M1_STORAGE_PAGE_BYTES];
static unsigned storage_allowed;
static bool storage_resumes,storage_owned;
static unsigned storage_begins,storage_ends,storage_writes,storage_erases;
static uint32_t storage_error;
uint32_t m1_test_recovery_result=M1_STORAGE_VERIFY;
uint32_t __wrap_m1_storage_check_recovery(bool allow_armed)
{ return allow_armed?m1_test_recovery_result:M1_STORAGE_ARGUMENT; }
uint32_t __wrap_m1_storage_read(unsigned slot,uint8_t *page)
{ if(slot>1)return M1_STORAGE_ARGUMENT;memcpy(page,pages[slot],sizeof(pages[0]));return 0; }
uint32_t __wrap_m1_storage_write(unsigned slot,const uint8_t *page,bool safe)
{
    if(slot>1 || !safe || !storage_owned)return M1_STORAGE_UNSAFE;
    ++storage_writes;if(storage_error)return storage_error;
    memcpy(pages[slot],page,sizeof(pages[0]));return 0;
}
/* The real erase verifies the whole page blank; the audit models that outcome
 * and counts the pages the owner asked for. */
uint32_t __wrap_m1_storage_erase(unsigned slot,bool safe)
{
    if(slot>1 || !safe || !storage_owned)return M1_STORAGE_UNSAFE;
    ++storage_erases;if(storage_error)return storage_error;
    memset(pages[slot],255,sizeof(pages[0]));return 0;
}
static m1_save_result_t storage_begin(void *context)
{
    (void)context;++storage_begins;
    if(storage_allowed!=M1_SAVE_READY)return storage_allowed;
    storage_owned=true;return M1_SAVE_READY;
}
static bool storage_end(void *context)
{ (void)context;++storage_ends;storage_owned=false;return storage_resumes; }
uintptr_t m1_test_live_storage(void)
{ static const m1_live_storage_ops_t ops={storage_begin,storage_end,NULL};return (uintptr_t)&ops; }
void m1_test_live_storage_gate(unsigned allowed,unsigned resumes,uint32_t error)
{ storage_allowed=allowed;storage_resumes=resumes!=0;storage_error=error; }
unsigned m1_test_live_storage_count(unsigned field)
{
    return field==0?storage_begins:field==1?storage_ends:
        field==2?storage_writes:storage_erases;
}
uintptr_t m1_test_live_storage_page(unsigned slot)
{ return slot<2?(uintptr_t)pages[slot]:0; }
__attribute__((used,section(".test_exports")))
const void *const m1_live_test_exports[]={
    m1_encoder_start,m1_encoder_irq,m1_encoder_status,m1_encoder_discard,
    m1_live_init,m1_live_service,m1_live_stop,m1_live_scan_losses,m1_live_transport,m1_live_transport_fault,
    m1_live_power_suspend,m1_live_power_park,m1_live_power_resume,
    m1_live_source_suspend,m1_live_source_resume,
    m1_live_power_activity,m1_wireless_resume_retained,
    m1_live_factory_result,m1_live_update_requested,
    m1_live_storage_fault,m1_test_live_storage,m1_test_live_storage_gate,
    m1_transport_ops,m1_wireless_selected,m1_wireless_switch_ready,m1_wireless_select,
    m1_test_live_storage_count,m1_test_live_storage_page,
    m1_test_live_frame,m1_test_live_periodic,m1_test_live_led,m1_test_live_get,
    m1_test_live_scan_errors,
    scan_stream_lost,scan_stream_dropped,
    m1_radio_init,m1_radio_service,m1_radio_healthy,m1_radio_ready,
    m1_wireless_init,m1_wireless_stop,m1_wireless_ready,m1_wireless_healthy,m1_wireless_local_idle,
    m1_wireless_reports_sent,m1_wireless_mode,m1_wireless_service,
    m1_wireless_request_sleep,m1_wireless_sleep_sent,
    m1_test_live_transports,m1_test_live_transport_gate,m1_test_live_selection,m1_test_live_battery
};
