#include "m1_live.h"
#include "m1_hal.h"
#include "m1_lighting.h"
#include "m1_battery_hal.h"
#include "m1_controls.h"
#include "m1_wireless.h"
#include "m1_usb.h"
#include "midi_control.h"
#include "scan_stream.h"
#include "device_store.h"
#include "m1_storage.h"
#include "m1_image.h"
#include "m1_transport.h"
#include "m1_encoder.h"
#include "keyboard_aux.h"
#include "at32f402_405_conf.h"
#include <string.h>

static keyboard_app_t app;
static keyboard_raw_t raw;
static keyboard_midi_t midi;
static keyboard_menu_t menu;
static keyboard_calibration_t calibration;
static m1_controls_t controls;
static uint16_t lower[M1_KEY_COUNT],upper[M1_KEY_COUNT],samples[M1_KEY_COUNT];
static uint8_t lights[M1_LED_BYTES];
static uint32_t now,epoch,scan_sequence,losses,last_gui,last_light;
static keyboard_telemetry_status_t status;
static bool initialized,enabled,seen,source_healthy,light_sent,selection_attempted,transport_fault;
static const m1_transport_ops_t *transport_ops;
static m1_factory_result_t factory_result=M1_FACTORY_NOT_LOADED;
static device_store_t store;
static const m1_live_storage_ops_t *storage_ops;
static bool storage_fault,storage_gap;
static bool update_requested;
static bool last_output_ready;
static keyboard_aux_t auxiliary;
static bool auxiliary_allowed;
static bool power_activity;
static bool usb_abandoned;
static const uint16_t auxiliary_mapping[3]={
#define AUXMAP(index,usage) [index]=usage,
#include "../config/auxmap.def"
#undef AUXMAP
};
bool m1_live_update_requested(void) { return update_requested; }
static uint32_t last_save_attempt;
/* Foreground wall-time, including interrupt preemption. Read the already owned
 * 1 MHz TMR2 through the SDK; never reset/reconfigure a peripheral for profiling.
 * Unsigned totals/counters wrap. MAX is since live initialization. */
enum { TIMING_HAL,TIMING_FRAME,TIMING_STORE,TIMING_OUTPUT,TIMING_CONTROLS,
       TIMING_LIGHTS,TIMING_CONTROL,TIMING_LOOP,TIMING_COUNT };
static struct { uint32_t calls,total,maximum; } timing[TIMING_COUNT];
static void timing_add(unsigned index,uint32_t elapsed)
{
    ++timing[index].calls;timing[index].total+=elapsed;
    if(elapsed>timing[index].maximum)timing[index].maximum=elapsed;
}
static uint32_t timing_step(unsigned index,uint32_t start)
{
    uint32_t end=tmr_counter_value_get(TMR2);timing_add(index,end-start);return end;
}
static void put32(uint8_t *out,uint32_t value)
{ for(unsigned i=0;i<4;++i)out[i]=(uint8_t)(value>>(8*i)); }
static enum { POWER_AWAKE,POWER_DRAINING,POWER_PARKED,POWER_STOPPED } power_state;
static keyboard_save_result_t save_calibration(const keyboard_calibration_t *cal);
static const keyboard_app_ops_t app_ops={.save_calibration=save_calibration};

static uint32_t lock(void) { uint32_t mask=__get_PRIMASK();__disable_irq();return mask; }
static void unlock(uint32_t mask) { __set_PRIMASK(mask); }
static uint32_t millis(void) { return now; }
static bool usb_ready(void) { return epoch==m1_usb_generation() && m1_usb_ready(); }
static bool radio_mode(m1_transport_t mode)
{ m1_transport_t configured;return m1_wireless_mode(&configured) && configured==mode; }
static bool output_ready(void)
{ return controls.current==M1_TRANSPORT_USB?usb_ready():
    radio_mode(controls.current) && m1_wireless_ready(); }
static bool send_events(const uint8_t *data,uint32_t size)
{
    uint32_t mask=lock();
    bool ok=usb_ready() && m1_usb_midi_send(data,size);
    unlock(mask);return ok;
}
static bool send_keyboard(const keyboard_report_t *report)
{
    if(controls.current!=M1_TRANSPORT_USB)
        return radio_mode(controls.current) && m1_wireless_offer(report);
    uint32_t mask=lock();
    bool ok=usb_ready() && m1_usb_hid_send(report);
    unlock(mask);return ok;
}
static bool send_midi(uint8_t a,uint8_t b,uint8_t c,uint8_t d)
{ uint8_t event[4]={a,b,c,d};return controls.current==M1_TRANSPORT_USB && send_events(event,sizeof(event)); }
static bool send_consumer(uint16_t usage)
{
    if(controls.current!=M1_TRANSPORT_USB)
        return radio_mode(controls.current) && m1_wireless_consumer(usage);
    return usb_ready() && m1_usb_consumer_send(usage);
}
static void service_auxiliary(bool allowed)
{
    m1_encoder_status_t input;
    if(!m1_encoder_status(&input))return;
    allowed=allowed && input.active && !input.fault;
    if(input.pressed || input.queued)power_activity=true;
    if(!allowed || !auxiliary_allowed) {
        /* Never replay offline/menu movement or a partial turn to a new host. */
        if(auxiliary_allowed || allowed || input.queued)m1_encoder_discard();
    }
    auxiliary_allowed=allowed;
    keyboard_aux_service(&auxiliary,allowed,now,send_consumer);
    if(allowed && keyboard_aux_idle(&auxiliary)) {
        uint8_t events;
        if(m1_encoder_take(&events))(void)keyboard_aux_offer(&auxiliary,events);
    }
}
static void cancel_input(void)
{
    if(selection_attempted) { transport_fault=true;enabled=false; }
    keyboard_app_invalidate(&app,now);
    keyboard_midi_abort(&midi);
    keyboard_aux_cancel(&auxiliary);auxiliary_allowed=false;m1_encoder_discard();
    controls.pending=controls.held=0;controls.switching=controls.battery_show=false;
    controls.neutral_required=true;keyboard_text_stop(&controls.text);
    light_sent=false;
}
static void check_epoch(void)
{
    uint32_t mask=lock(),current=m1_usb_generation();
    bool changed=current!=epoch;
    if(changed) { epoch=current;midi_control_usb_reset();scan_stream_usb_reset(); }
    unlock(mask);
    /* A USB GUI reset must not release a key owned by a wireless host. */
    if(changed && controls.current==M1_TRANSPORT_USB && !selection_attempted)cancel_input();
}
static bool drained(void *context)
{
    (void)context;
    /* The release proof belongs to this switch. Once selection begins the
     * old driver may legitimately be stopped; never ask it to drain again. */
    if(selection_attempted)return true;
    if(!keyboard_aux_idle(&auxiliary))return false;
    bool idle=controls.current==M1_TRANSPORT_USB?usb_ready() && m1_usb_drained():
        radio_mode(controls.current) && m1_wireless_switch_ready();
    return idle && transport_ops->drained(transport_ops->context);
}
static bool select_transport(void *context,m1_transport_t target)
{
    (void)context;
    selection_attempted=true;
    if(!transport_ops->select(transport_ops->context,target))return false;
    /* Caller confirmation cannot substitute for real endpoint/peer readiness. */
    return target==M1_TRANSPORT_USB?usb_ready():radio_mode(target) && m1_wireless_selected(target);
}
static bool transport_available(void *context,m1_transport_t target)
{ (void)context;return !transport_ops->available || transport_ops->available(transport_ops->context,target); }
static bool decimal(const char **text,uint32_t *value)
{
    const char *p=*text;if(*p<'0' || *p>'9')return false;
    uint32_t n=0;
    while(*p>='0' && *p<='9') {
        unsigned digit=*p++-'0';if(n>(UINT32_MAX-digit)/10u)return false;
        n=n*10u+digit;
    }
    *text=p;*value=n;return true;
}
bool m1_live_publish_stats(void)
{
    if(!initialized)return false;
    uint8_t payload[24u+12u*TIMING_COUNT]={'M','1','P','F',1,TIMING_COUNT};
    payload[6]=sizeof(payload);payload[7]=sizeof(payload)>>8;
    put32(payload+8,now);put32(payload+12,scan_sequence);
    put32(payload+16,losses);put32(payload+20,m1_hal_errors());
    for(unsigned i=0;i<TIMING_COUNT;++i) {
        put32(payload+24+12*i,timing[i].calls);
        put32(payload+28+12*i,timing[i].total);
        put32(payload+32+12*i,timing[i].maximum);
    }
    return midi_control_publish(MT_DUMP,payload,sizeof(payload));
}
static bool power_status(keyboard_power_status_t *out)
{
    if(!enabled || !usb_ready())return false;
    const m1_battery_t *b=m1_battery_hal_status();
    static const uint8_t chargers[]={MT_CHARGE_UNKNOWN,MT_CHARGE_BATTERY,
                                   MT_CHARGE_RAW_LOW,MT_CHARGE_RAW_HIGH};
    if((unsigned)b->charger>=sizeof(chargers))return false;
    *out=(keyboard_power_status_t){
        .flags=(b->source_known?MT_POWER_SOURCE_KNOWN:0u) |
               (b->externally_powered?MT_POWER_EXTERNAL:0u) |
               (b->valid?MT_POWER_VALID:0u) |
               (m1_battery_low(b)?MT_POWER_LOW:0u) |
               (m1_battery_critical(b)?MT_POWER_CRITICAL:0u),
        .percent=b->valid?b->percent:0u,.charger=chargers[b->charger],
        .adc=b->valid?b->average:UINT16_MAX,
        .age_ms=b->sample_clock?(uint32_t)(now-b->sampled_at):UINT32_MAX};
    return true;
}
static bool command(const char *line)
{
    if(!enabled || !usb_ready())return false;
    if(!strcmp(line,"runtime encoder")) {
        m1_encoder_status_t input;
        if(!m1_encoder_status(&input))return false;
        uint8_t payload[32]={'M','1','E','N',1,0,32,0};
        payload[5]=input.active | (input.fault<<1) | (input.pressed<<2);
        payload[8]=input.phase;payload[9]=input.queued;
        put32(payload+12,input.samples);put32(payload+16,input.positive);
        put32(payload+20,input.negative);put32(payload+24,input.invalid);
        put32(payload+28,input.overflows);
        return midi_control_publish(MT_DUMP,payload,sizeof(payload));
    }
    if(!strcmp(line,"runtime stats"))return m1_live_publish_stats();
    if(!strcmp(line,"factory read")) {
        uint8_t payload[M1_FACTORY_DUMP_BYTES];
        return m1_factory_read_dump(payload)==M1_FACTORY_OK &&
            midi_control_publish(MT_DUMP,payload,sizeof(payload));
    }
    if(!strcmp(line,"bootloader")) {
        if(*(const volatile uint32_t *)M1_RECOVERY_FLAG_ADDRESS!=M1_RECOVERY_FLAG_VALUE)return false;
        update_requested=true;return true;
    }
    if(!strcmp(line,"stream gui")) { scan_stream_gui();return true; }
    if(!strcmp(line,"stream off")) { scan_stream_stop();return true; }
    if(!strncmp(line,"stream key ",11)) {
        const char *p=line+11;uint32_t threshold,session,sensor;
        if(!decimal(&p,&threshold) || *p++!=' ' || !decimal(&p,&session) ||
           *p++!=' ' || !decimal(&p,&sensor) || *p || !threshold || threshold>4096u ||
           sensor>=M1_KEY_COUNT || !source_healthy || !seen || !app.frame_valid ||
           (uint32_t)(now-app.last_frame)>=SCAN_STALE_MS)return false;
        scan_stream_last_key(threshold,session,sensor);return true;
    }
    return keyboard_app_command(&app,line,now,source_healthy && usb_ready() && !store.fault,
                                &status.ack,&status.result);
}
bool m1_live_init(m1_transport_t current,const m1_transport_ops_t *transports,
                  const m1_live_storage_ops_t *storage,const uint16_t *released)
{
    const keyboard_report_t neutral={0};
    bool old_drained=!initialized || (controls.current==M1_TRANSPORT_USB?
        usb_ready() && m1_usb_drained() && !midi.panic && !midi.count:
        transport_ops && drained(NULL));
    if((initialized && (enabled || power_state!=POWER_AWAKE || transport_fault || storage_fault || !old_drained || !keyboard_aux_idle(&auxiliary) || !app.sent_valid ||
        memcmp(&app.sent,&neutral,sizeof(neutral)))) || !m1_transport_valid(current) ||
       (current!=M1_TRANSPORT_USB && !radio_mode(current)) ||
       (transports && (!transports->drained || !transports->select)) ||
       (storage && (!storage->begin || !storage->end)))return false;
    device_store_load(&store,M1_PROFILE,M1_KEY_COUNT,lower,upper,m1_storage_read);
    m1_factory_bounds_t bounds;
    factory_result=m1_factory_load(&bounds);
    if(!store.saved && factory_result!=M1_FACTORY_OK) {
        if((factory_result!=M1_FACTORY_MARKER && factory_result!=M1_FACTORY_UNCALIBRATED &&
            factory_result!=M1_FACTORY_RANGE) || !m1_factory_bootstrap(released,&bounds))return false;
    }
    static const midi_control_port_t port={millis,usb_ready,send_events,lock,unlock};
    static const m1_transport_ops_t transport_port={drained,select_transport,NULL,transport_available};
    keyboard_app_init(&app,&raw,&midi,&menu,&calibration,storage?&app_ops:NULL);
    keyboard_aux_init(&auxiliary,auxiliary_mapping);auxiliary_allowed=false;
    /* Bind layout/roles without treating a fabricated sample as acquisition.
     * Restore every setting before any real frame or output can be processed. */
    memset(samples,0,sizeof(samples));
    keyboard_raw_frame(&raw,samples,M1_KEY_COUNT,M1_PROFILE,false);
    keyboard_midi_frame(&midi,&raw,lower,upper,0);
    (void)device_store_apply(&store,&app);
    transport_ops=transports;storage_ops=storage;
    (void)m1_controls_bind(&controls,&app,current,transports?&transport_port:NULL,m1_battery_hal_status());
    if(!store.saved) {
        memcpy(lower,bounds.lower,sizeof(lower));memcpy(upper,bounds.upper,sizeof(upper));
    }
    now=scan_sequence=losses=last_gui=last_light=last_save_attempt=0;
    memset(timing,0,sizeof(timing));
    seen=source_healthy=light_sent=selection_attempted=transport_fault=storage_gap=false;
    update_requested=power_activity=usb_abandoned=false;
    power_state=POWER_AWAKE;
    status=(keyboard_telemetry_status_t){.storage_slot=255,
                                      .calibration_saved=store.saved || factory_result==M1_FACTORY_OK,
                                      .calibration_supported=storage!=NULL};
    epoch=m1_usb_generation();scan_stream_init();
    if(!midi_control_init(&port))return false;
    midi_control_command_handler(command);midi_control_power_handler(power_status);
    initialized=enabled=true;
    last_output_ready=output_ready();return true;
}
void m1_live_stop(uint32_t now_ms)
{
    if(!initialized || (!enabled && power_state==POWER_AWAKE))return;
    now=now_ms;enabled=false;
    /* Do not reclaim peripherals already handed to the power owner. */
    power_state=power_state>=POWER_PARKED?POWER_STOPPED:POWER_AWAKE;
    cancel_input();scan_stream_stop();midi_control_usb_reset();
}
static bool neutral_sent(void)
{
    const keyboard_report_t neutral={0};
    return app.sent_valid && !memcmp(&app.sent,&neutral,sizeof(neutral));
}
bool m1_live_power_suspend(uint32_t now_ms)
{
    if(!initialized || power_state==POWER_STOPPED || transport_fault || storage_fault ||
       selection_attempted || controls.switching)return false;
    if(power_state!=POWER_AWAKE)return true;
    if(!enabled)return false;
    now=now_ms;enabled=false;power_state=POWER_DRAINING;
    cancel_input();scan_stream_lost();++losses;seen=source_healthy=false;
    scan_stream_stop();midi_control_usb_reset();return true;
}
bool m1_live_power_park(void)
{
    if(!initialized || transport_fault || storage_fault)return false;
    if(power_state==POWER_PARKED)return true;
    bool detached=usb_abandoned && controls.current==M1_TRANSPORT_USB;
    if(power_state!=POWER_DRAINING || (!detached && (!neutral_sent() || !keyboard_aux_idle(&auxiliary))) ||
       !m1_lighting_healthy() || !m1_lighting_ready())return false;
    uint32_t mask=lock();
    bool ready=m1_usb_in_idle() && (controls.current==M1_TRANSPORT_USB?
        detached || (usb_ready() && !midi.panic && !midi.count):
        radio_mode(controls.current) && m1_wireless_switch_ready());
    if(ready)power_state=POWER_PARKED;
    unlock(mask);return ready;
}
bool m1_live_power_activity(bool *activity)
{
    if(!activity || !initialized || !enabled || power_state!=POWER_AWAKE ||
       !source_healthy || !seen || !app.frame_valid || controls.switching ||
       selection_attempted || storage_fault || transport_fault ||
       (uint32_t)(now-app.last_frame)>=SCAN_STALE_MS)return false;
    *activity=power_activity || !raw.neutral_idle;
    power_activity=false;return true;
}
bool m1_live_source_suspend(uint32_t now_ms,bool usb_disconnected)
{
    if(usb_disconnected && (m1_usb_ready() || !m1_usb_in_idle()))return false;
    if(!m1_live_power_suspend(now_ms))return false;
    if(usb_disconnected && controls.current==M1_TRANSPORT_USB) {
        usb_abandoned=true;
        /* The endpoint owner aborted the old consumer transaction. Preserve
         * its map, not pending pulses for a host that is no longer present. */
        uint16_t mapping[3];memcpy(mapping,auxiliary.mapping,sizeof(mapping));
        keyboard_aux_init(&auxiliary,mapping);
    }
    return true;
}
static bool power_resume(uint32_t now_ms,m1_transport_t target,bool platform_restored,bool source_change)
{
    if(!initialized || power_state!=POWER_PARKED || !platform_restored ||
       transport_fault || storage_fault || !m1_hal_periodic_active() ||
       !m1_lighting_healthy() || !m1_transport_valid(target) ||
       (target!=controls.current && (!source_change || !usb_abandoned ||
        controls.current!=M1_TRANSPORT_USB || target==M1_TRANSPORT_USB)))return false;
    if(target==M1_TRANSPORT_USB?!source_change && !usb_ready():
       !radio_mode(target) || !m1_wireless_selected(target))return false;
    controls.current=controls.target=target;
    menu.midi_blocked=target!=M1_TRANSPORT_USB;
    if(menu.midi_blocked && midi.mode)keyboard_midi_toggle(&midi,&raw,now_ms);
    now=now_ms;check_epoch();
    /* A searching wireless peer is a valid restored transport. Require a
     * fresh matching mode, not a connected host; the ordinary readiness edge
     * still cancels offline input before any newly connected host can type. */
    uint32_t discarded;
    (void)m1_hal_frame(samples,&discarded);
    uint8_t events[M1_USB_HS_PACKET];
    (void)m1_usb_midi_take(events,sizeof(events));
    scan_stream_usb_reset();midi_control_usb_reset();
    /* Do not restart MIDI panic here: park already proved cleanup completed.
     * A changed USB epoch may legitimately have queued fresh cleanup. */
    keyboard_app_invalidate(&app,now);seen=source_healthy=light_sent=false;
    controls.neutral_required=true;
    power_state=POWER_AWAKE;enabled=true;usb_abandoned=false;return true;
}
bool m1_live_power_resume(uint32_t now_ms,bool platform_restored)
{ return power_resume(now_ms,controls.current,platform_restored,false); }
bool m1_live_source_resume(uint32_t now_ms,m1_transport_t target,bool platform_restored)
{ return power_resume(now_ms,target,platform_restored,true); }
uint32_t m1_live_scan_losses(void) { return losses; }
m1_factory_result_t m1_live_factory_result(void) { return factory_result; }
m1_transport_t m1_live_transport(void) { return controls.current; }
bool m1_live_transport_fault(void) { return transport_fault; }
bool m1_live_storage_fault(void) { return storage_fault; }
uint32_t m1_live_storage_error(void) { return store.error; }
static uint32_t write_profile(unsigned slot,const uint8_t *page)
{ return m1_storage_write(slot,page,true); }
static bool storage_idle(void)
{
    if(!enabled || store.fault || !storage_ops ||
       (uint32_t)(now-last_save_attempt)<SETTINGS_CHECK_PERIOD_MS ||
       controls.switching || controls.pending || selection_attempted || !app.sent_valid ||
       !m1_lighting_ready())return false;
    m1_encoder_status_t input;
    if(!keyboard_aux_idle(&auxiliary) || !m1_encoder_status(&input) || input.queued || input.pressed)return false;
    const keyboard_report_t neutral={0};
    if(memcmp(&app.sent,&neutral,sizeof(neutral)))return false;
    if(controls.current==M1_TRANSPORT_USB) {
        if(!usb_ready() || !m1_usb_drained() || midi.panic || midi.count)return false;
    } else if(!radio_mode(controls.current) || !m1_wireless_local_idle())return false;
    return true;
}
static void storage_failure(uint32_t error)
{ storage_fault=true;enabled=false;store.fault=true;store.error=error; }
static keyboard_save_result_t commit_profile(const keyboard_calibration_t *cal)
{
    last_save_attempt=now;
    m1_save_result_t started=storage_ops->begin(storage_ops->context);
    if(started==M1_SAVE_DEFER)return KEYBOARD_SAVE_DEFER;
    storage_gap=true;
    if(started!=M1_SAVE_READY) {
        storage_failure(M1_STORAGE_QUIESCE);return KEYBOARD_SAVE_FAILED;
    }
    bool saved=device_store_update(&store,&app,cal,m1_storage_read,write_profile);
    bool resumed=storage_ops->end(storage_ops->context);
    if(!resumed)storage_failure(M1_STORAGE_RESUME);
    return saved && resumed?KEYBOARD_SAVE_COMPLETE:KEYBOARD_SAVE_FAILED;
}
static keyboard_save_result_t save_calibration(const keyboard_calibration_t *cal)
{
    if(!enabled || !source_healthy || !seen || !app.frame_valid || store.fault ||
       !storage_ops || (uint32_t)(now-app.last_frame)>=SCAN_STALE_MS)
        return KEYBOARD_SAVE_FAILED;
    /* Completed keys may stay held. Only host outputs, not physical samples,
     * must be neutral. Shared CAL_SAVE retains and validates the candidate
     * during bounded deferral; no application mutation inside this callback. */
    if(!storage_idle())return KEYBOARD_SAVE_DEFER;
    return commit_profile(cal);
}
static bool publish_storage_gap(void)
{
    if(!storage_gap)return false;
    storage_gap=false;scan_stream_lost();++losses;seen=false;cancel_input();
    return true;
}
static bool persist(bool fresh)
{
    if(!enabled || !fresh)return false;
    (void)device_store_poll(&store,&app,now,false);
    if(!store.pending || (uint32_t)(now-store.changed_at)<SETTINGS_SAVE_QUIET_MS ||
       !storage_idle())return false;
    /* Recheck the entire snapshot: a new edit must restart debounce. Unlike
     * explicit calibration, autosave also requires all physical keys neutral. */
    if(!device_store_poll(&store,&app,now,true))return false;
    (void)commit_profile(NULL);
    return publish_storage_gap();
}
static void snapshot(void)
{
    if(!scan_stream_gui_enabled() || (uint32_t)(now-last_gui)<GUI_REPORT_PERIOD_MS)return;
    last_gui=now;status.now=now;++status.sequence;
    status.calibration_saved=store.saved || factory_result==M1_FACTORY_OK;
    status.scan_errors=m1_hal_errors()+losses;
    status.scan_fault=!source_healthy || !seen || (uint32_t)(now-app.last_frame)>=SCAN_STALE_MS;
    status.light_errors=m1_lighting_errors();status.light_fault=!m1_lighting_healthy();
    status.calibration_generation=store.calibration_generation;
    status.storage_error=store.error;status.storage_generation=store.generation;
    status.storage_flags=store.valid | (store.pending<<1u) | (store.fault<<2u);
    status.storage_slot=store.slot;
    status.transport=controls.current==M1_TRANSPORT_USB?MT_TRANSPORT_USB:
        controls.current==M1_TRANSPORT_RADIO?MT_TRANSPORT_RADIO:MT_TRANSPORT_BT1+controls.current;
    status.transport_flags=(output_ready()?MT_TRANSPORT_READY:0u) |
        (controls.switching?MT_TRANSPORT_SWITCHING:0u);
    uint8_t out[SCAN_STREAM_GUI_SIZE];
    size_t size=keyboard_telemetry_encode(&app,&status,out,sizeof(out));
    if(size)(void)scan_stream_gui_push(out,size);
}
void m1_live_service(uint32_t now_ms,uint32_t now_us)
{
    if(!initialized || power_state>=POWER_PARKED)return;
    uint32_t started=tmr_counter_value_get(TMR2),mark=started;
    now=now_ms;check_epoch();
    m1_transport_service(now_us);
    m1_hal_service(now_us);m1_lighting_service(now_us);m1_battery_hal_service(now);
    m1_wireless_service(now_us);
    mark=timing_step(TIMING_HAL,mark);
    if(power_state==POWER_DRAINING) {
        /* No new scan, configuration, battery packet, LED frame or flash
         * transaction may compete with the neutral-output handoff. */
        /* Acquisition stays owned by the outer power controller until park.
         * Host backpressure can outlast the scan FIFO: consume and discard
         * frames while draining, never feed them to keys/velocity/capture.
         * The suspend boundary has already invalidated those consumers. */
        uint32_t discarded;
        (void)m1_hal_frame(samples,&discarded);
        keyboard_app_service(&app,now,false,usb_abandoned || neutral_sent()?NULL:send_keyboard,
                             controls.current==M1_TRANSPORT_USB && !usb_abandoned?send_midi:NULL);
        service_auxiliary(false);
        uint8_t events[M1_USB_HS_PACKET];
        (void)m1_usb_midi_take(events,sizeof(events)); /* discard, never dispatch */
        midi_control_service();return;
    }
    if(controls.current!=M1_TRANSPORT_USB)
        (void)m1_wireless_battery(m1_battery_hal_status());
    source_healthy=enabled && m1_hal_periodic_active();
    bool ready=output_ready();
    /* Menus remain usable before a wireless host connects. A readiness edge
     * cancels any offline-held input so it cannot be replayed to the new host.
     * An authorized in-progress selection owns its own neutral handoff. */
    if(ready!=last_output_ready && !controls.switching)cancel_input();
    last_output_ready=ready;
    if(store.fault)menu.disabled_options|=1u<<(MENU_CALIBRATION-1u);
    uint32_t sequence;
    if(source_healthy && m1_hal_frame(samples,&sequence)) {
        if(seen && sequence-scan_sequence!=1u) {
            ++losses;scan_stream_lost();cancel_input();
        }
        seen=true;scan_sequence=sequence;
        keyboard_app_frame(&app,samples,M1_KEY_COUNT,M1_PROFILE,lower,upper,
                           true,now);
        power_activity|=!raw.neutral_idle;
        /* The save callback has now finished publishing/discarding bounds.
         * Invalidation inside it would destroy the candidate prematurely. */
        if(publish_storage_gap()) { snapshot();timing_step(TIMING_FRAME,mark);timing_step(TIMING_LOOP,started);return; }
        /* Capture and GUI values share the control domain used for velocity,
         * not the electrical samples retained for calibration. */
        scan_stream_push(raw.raw,M1_KEY_COUNT,M1_PROFILE,now_us);
    }
    mark=timing_step(TIMING_FRAME,mark);
    bool fresh=source_healthy && seen && (uint32_t)(now-app.last_frame)<SCAN_STALE_MS;
    if(!fresh)scan_stream_lost();
    /* Before queuing this iteration's heartbeat/GUI/LED traffic, so the
     * periodic output refresh cannot starve an otherwise idle pending save. */
    bool saved=persist(fresh);mark=timing_step(TIMING_STORE,mark);
    if(saved) { snapshot();timing_step(TIMING_LOOP,started);return; }
    /* Keyboard/performance traffic has first use of each endpoint. MIDI
     * control chunks are bounded and never overwrite an in-flight report. */
    keyboard_app_service(&app,now,fresh,send_keyboard,
                         controls.current==M1_TRANSPORT_USB?send_midi:NULL);
    service_auxiliary(fresh && output_ready() && raw.enabled && raw.armed &&
        !raw.down[M1_FN_SENSOR] && !controls.switching && !controls.pending &&
        !calibration_active(&calibration) && !raw.engine.config.mode &&
        !menu.pending && !menu.music_page && !menu.velocity_page && !menu.press_page && !menu.reset_confirmation);
    mark=timing_step(TIMING_OUTPUT,mark);
    bool switching=controls.switching;
    m1_transport_t old=controls.current;
    m1_controls_service(&controls,&app,now);
    if(switching && !controls.switching) {
        keyboard_aux_cancel(&auxiliary);auxiliary_allowed=false;m1_encoder_discard();
        if(selection_attempted && old==controls.current) {
            /* A timed-out/cancelled physical selection may have changed the
             * hardware already. Do not silently resume on the old host. */
            transport_fault=true;m1_live_stop(now);
        }
        selection_attempted=false;
    }
    mark=timing_step(TIMING_CONTROLS,mark);
    if(m1_lighting_ready() && (!light_sent || (uint32_t)(now-last_light)>=LIGHTING_FRAME_PERIOD_MS)) {
        keyboard_app_lights(&app,lower,upper,lights,now);
        if(m1_lighting_offer(lights,sizeof(lights),now_us)) { last_light=now;light_sent=true; }
    }
    mark=timing_step(TIMING_LIGHTS,mark);
    uint8_t events[M1_USB_HS_PACKET];unsigned n=m1_usb_midi_take(events,sizeof(events));
    uint32_t mask=lock();
    if(n && usb_ready())midi_control_receive_usb(events,n);
    unlock(mask);
    check_epoch();
    midi_control_service();snapshot();(void)scan_stream_service();
    timing_step(TIMING_CONTROL,mark);timing_step(TIMING_LOOP,started);
}
