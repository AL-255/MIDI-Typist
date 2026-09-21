#include "m1_live.h"
#include "m1_hal.h"
#include "m1_lighting.h"
#include "m1_battery_hal.h"
#include "m1_controls.h"
#include "m1_wireless.h"
#include "m1_usb.h"
#include "midi_control.h"
#include "scan_stream.h"
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
        return output_ready() && m1_wireless_offer(report);
    uint32_t mask=lock();
    bool ok=usb_ready() && m1_usb_hid_send(report);
    unlock(mask);return ok;
}
static bool send_midi(uint8_t a,uint8_t b,uint8_t c,uint8_t d)
{ uint8_t event[4]={a,b,c,d};return controls.current==M1_TRANSPORT_USB && send_events(event,sizeof(event)); }
static void cancel_input(void)
{
    if(selection_attempted) { transport_fault=true;enabled=false; }
    keyboard_app_invalidate(&app,now);
    keyboard_midi_abort(&midi);
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
    bool idle=controls.current==M1_TRANSPORT_USB?usb_ready() && m1_usb_drained():
        radio_mode(controls.current) && m1_wireless_local_idle();
    return idle && transport_ops->drained(transport_ops->context);
}
static bool select_transport(void *context,m1_transport_t target)
{
    (void)context;
    selection_attempted=true;
    if(!transport_ops->select(transport_ops->context,target))return false;
    /* Caller confirmation cannot substitute for real endpoint/peer readiness. */
    return target==M1_TRANSPORT_USB?usb_ready():radio_mode(target) && m1_wireless_ready();
}
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
static bool command(const char *line)
{
    if(!enabled || !usb_ready())return false;
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
    return keyboard_app_command(&app,line,now,source_healthy && usb_ready(),&status.ack,&status.result);
}
bool m1_live_init(m1_transport_t current,const m1_transport_ops_t *transports)
{
    const keyboard_report_t neutral={0};
    bool old_drained=!initialized || (controls.current==M1_TRANSPORT_USB?
        usb_ready() && m1_usb_drained() && !midi.panic && !midi.count:
        transport_ops && drained(NULL));
    if((initialized && (enabled || transport_fault || !old_drained || !app.sent_valid ||
        memcmp(&app.sent,&neutral,sizeof(neutral)))) || !m1_transport_valid(current) ||
       (current!=M1_TRANSPORT_USB && !radio_mode(current)) ||
       (transports && (!transports->drained || !transports->select)))return false;
    m1_factory_bounds_t bounds;
    factory_result=m1_factory_load(&bounds);
    if(factory_result!=M1_FACTORY_OK)return false;
    static const midi_control_port_t port={millis,usb_ready,send_events,lock,unlock};
    static const m1_transport_ops_t transport_port={drained,select_transport,NULL};
    keyboard_app_init(&app,&raw,&midi,&menu,&calibration,NULL);
    transport_ops=transports;
    (void)m1_controls_bind(&controls,&app,current,transports?&transport_port:NULL,m1_battery_hal_status());
    memcpy(lower,bounds.lower,sizeof(lower));memcpy(upper,bounds.upper,sizeof(upper));
    now=scan_sequence=losses=last_gui=last_light=0;
    seen=source_healthy=light_sent=selection_attempted=transport_fault=false;
    status=(keyboard_telemetry_status_t){.storage_slot=255,.calibration_saved=true};
    epoch=m1_usb_generation();scan_stream_init();
    if(!midi_control_init(&port))return false;
    midi_control_command_handler(command);initialized=enabled=true;return true;
}
void m1_live_stop(uint32_t now_ms)
{
    if(!initialized || !enabled)return;
    now=now_ms;enabled=false;cancel_input();scan_stream_stop();midi_control_usb_reset();
}
uint32_t m1_live_scan_losses(void) { return losses; }
m1_factory_result_t m1_live_factory_result(void) { return factory_result; }
m1_transport_t m1_live_transport(void) { return controls.current; }
bool m1_live_transport_fault(void) { return transport_fault; }
static void snapshot(void)
{
    if(!scan_stream_gui_enabled() || (uint32_t)(now-last_gui)<GUI_REPORT_PERIOD_MS)return;
    last_gui=now;status.now=now;++status.sequence;
    status.scan_errors=m1_hal_errors()+losses;
    status.scan_fault=!source_healthy || !seen || (uint32_t)(now-app.last_frame)>=SCAN_STALE_MS;
    status.light_errors=m1_lighting_errors();status.light_fault=!m1_lighting_healthy();
    uint8_t out[SCAN_STREAM_GUI_SIZE];
    size_t size=keyboard_telemetry_encode(&app,&status,out,sizeof(out));
    if(size)(void)scan_stream_gui_push(out,size);
}
void m1_live_service(uint32_t now_ms,uint32_t now_us)
{
    if(!initialized)return;
    now=now_ms;check_epoch();
    m1_hal_service(now_us);m1_lighting_service(now_us);m1_battery_hal_service(now);
    m1_wireless_service(now_us);
    if(controls.current!=M1_TRANSPORT_USB)
        (void)m1_wireless_battery(m1_battery_hal_status());
    source_healthy=enabled && m1_hal_periodic_active();
    uint32_t sequence;
    if(source_healthy && m1_hal_frame(samples,&sequence)) {
        if(seen && sequence-scan_sequence!=1u) {
            ++losses;scan_stream_lost();cancel_input();
        }
        seen=true;scan_sequence=sequence;
        keyboard_app_frame(&app,samples,M1_KEY_COUNT,M1_PROFILE,lower,upper,
                           controls.switching || output_ready(),now);
        scan_stream_push(samples,M1_KEY_COUNT,M1_PROFILE,now_us);
    }
    bool fresh=source_healthy && seen && (uint32_t)(now-app.last_frame)<SCAN_STALE_MS;
    if(!fresh)scan_stream_lost();
    /* Keyboard/performance traffic has first use of each endpoint. MIDI
     * control chunks are bounded and never overwrite an in-flight report. */
    keyboard_app_service(&app,now,fresh && (controls.switching || output_ready()),send_keyboard,
                         controls.current==M1_TRANSPORT_USB?send_midi:NULL);
    bool switching=controls.switching;
    m1_transport_t old=controls.current;
    m1_controls_service(&controls,&app,now);
    if(switching && !controls.switching) {
        if(selection_attempted && old==controls.current) {
            /* A timed-out/cancelled physical selection may have changed the
             * hardware already. Do not silently resume on the old host. */
            transport_fault=true;m1_live_stop(now);
        }
        selection_attempted=false;
    }
    if(m1_lighting_ready() && (!light_sent || (uint32_t)(now-last_light)>=LIGHTING_FRAME_PERIOD_MS)) {
        keyboard_app_lights(&app,lower,upper,lights,now);
        if(m1_lighting_offer(lights,sizeof(lights),now_us)) { last_light=now;light_sent=true; }
    }
    uint8_t events[M1_USB_HS_PACKET];unsigned n=m1_usb_midi_take(events,sizeof(events));
    uint32_t mask=lock();
    if(n && usb_ready())midi_control_receive_usb(events,n);
    unlock(mask);
    check_epoch();
    midi_control_service();snapshot();(void)scan_stream_service();
}
