#include "m1_controls.h"
#include "keyboard_lighting.h"
#include <string.h>

static const struct { uint8_t sensor; m1_transport_t target; const char *label; } choices[]={
    {1,M1_TRANSPORT_BT1,"BT1"},{2,M1_TRANSPORT_BT2,"BT2"},
    {3,M1_TRANSPORT_BT3,"BT3"},{4,M1_TRANSPORT_RADIO,"2.4G"},
    {5,M1_TRANSPORT_USB,"USB"},
};
enum { BATTERY_SENSOR=M1_SPACE_SENSOR, CHOICE_COUNT=sizeof(choices)/sizeof(choices[0]) };
static bool available(const m1_controls_t *s,m1_transport_t target)
{ return s->ops && s->ops->select && s->ops->drained &&
    (!s->ops->available || s->ops->available(s->ops->context,target)); }
bool m1_transport_valid(unsigned transport)
{
    for(unsigned i=0;i<CHOICE_COUNT;++i)if(choices[i].target==transport)return true;
    return false;
}
static void keyboard_only(keyboard_app_t *app,uint32_t now)
{
    if(app->midi->mode)keyboard_midi_toggle(app->midi,app->raw,now);
}
static void consume(keyboard_app_t *app)
{ keyboard_menu_cancel(app->menu); keyboard_raw_invalidate(app->raw); }
static bool system_input(keyboard_app_t *app,void *context,uint32_t now)
{
    m1_controls_t *s=context;
    keyboard_raw_t *raw=app->raw;
    bool wireless=s->current!=M1_TRANSPORT_USB;
    app->menu->midi_blocked=wireless || s->switching;
    if(wireless || s->switching)keyboard_only(app,now);
    if(!app->frame_valid || !raw->enabled || raw->profile!=M1_PROFILE ||
       raw->count!=M1_KEY_COUNT || calibration_active(app->cal) || raw->engine.config.mode) {
        s->pending=s->held=0; s->battery_show=false;
        keyboard_text_stop(&s->text); return false;
    }
    if(s->switching) { consume(app); return true; }
    if(s->neutral_required) {
        bool neutral=true;
        for(unsigned i=0;i<raw->count;++i)if(raw->raw[i]<=raw->release[i])neutral=false;
        if(neutral)s->neutral_required=false;
        consume(app); return true;
    }
    uint8_t held=0;
    for(unsigned i=0;i<CHOICE_COUNT+1u;++i) {
        unsigned sensor=i==CHOICE_COUNT?BATTERY_SENSOR:choices[i].sensor;
        if(s->held&(1u<<i)?raw->raw[sensor]<=raw->release[sensor]:
                           raw->raw[sensor]<raw->press[sensor])held|=1u<<i;
    }
    unsigned edges=held & ~s->held;
    s->held=held;
    bool fn=raw->raw[M1_FN_SENSOR]<=raw->release[M1_FN_SENSOR];
    if(s->pending) {
        unsigned index=s->pending-1u;
        bool cancelled=s->revision!=raw->revision;
        if(!cancelled && fn && (held&(1u<<index))) { consume(app); return true; }
        s->pending=0; s->battery_show=false; keyboard_text_stop(&s->text);
        s->neutral_required=true;
        if(!cancelled && index<CHOICE_COUNT && s->current!=choices[index].target) {
            s->target=choices[index].target; s->switching=true; s->requested_at=now;
            keyboard_only(app,now); app->sent_valid=false;
        }
        consume(app); return true;
    }
    if(!raw->armed || !raw->down[M1_FN_SENSOR] || !edges || (edges&(edges-1u)))return false;
    unsigned index=0;
    while(!(edges&(1u<<index)))++index;
    if(index<CHOICE_COUNT && !available(s,choices[index].target))return false;
    s->pending=index+1u; s->revision=raw->revision;
    s->battery_show=index==CHOICE_COUNT;
    if(!s->battery_show)keyboard_text_start(&s->text,raw->profile,choices[index].label,now);
    consume(app); return true;
}
static void system_lights(keyboard_app_t *app,void *context,uint8_t *frame,uint32_t now)
{
    m1_controls_t *s=context;
    if(calibration_active(app->cal))return;
    if(keyboard_text_render(&s->text,frame,now))return;
    if(app->raw->armed && app->raw->down[M1_FN_SENSOR]) {
        if(s->ops && s->ops->select && s->ops->drained)
            for(unsigned i=0;i<CHOICE_COUNT;++i) {
                if(!available(s,choices[i].target))continue;
                if(s->current==choices[i].target)
                    keyboard_light_set(M1_PROFILE,choices[i].sensor,frame,COLOR_CONFIRM);
                else keyboard_light_set(M1_PROFILE,choices[i].sensor,frame,COLOR_WHITE);
            }
        keyboard_light_set(M1_PROFILE,BATTERY_SENSOR,frame,COLOR_WHITE);
    }
    if(s->battery)m1_battery_lights(s->battery,s->battery_show,frame,now);
    else if(s->battery_show) {
        memset(frame,0,M1_LED_BYTES);
        keyboard_light_set(M1_PROFILE,BATTERY_SENSOR,frame,COLOR_RAPID);
    }
}
bool m1_controls_bind(m1_controls_t *s,keyboard_app_t *app,m1_transport_t current,
                       const m1_transport_ops_t *ops,const m1_battery_t *battery)
{
    if(!s || !app || !m1_transport_valid(current))return false;
    *s=(m1_controls_t){.current=current,.target=current,.ops=ops,.battery=battery};
    app->system_input=system_input; app->system_lights=system_lights; app->system_context=s;
    app->menu->midi_blocked=current!=M1_TRANSPORT_USB;
    if(app->menu->midi_blocked)keyboard_only(app,app->last_frame);
    return true;
}
void m1_controls_service(m1_controls_t *s,keyboard_app_t *app,uint32_t now)
{
    if(!app->frame_valid || (uint32_t)(now-app->last_frame)>=SCAN_STALE_MS) {
        if(s->switching)++s->errors;
        s->pending=s->held=0; s->switching=s->battery_show=false;
        s->neutral_required=true; keyboard_text_stop(&s->text);
        return;
    }
    if(!s->switching)return;
    if((uint32_t)(now-s->requested_at)>=M1_TRANSPORT_SWITCH_TIMEOUT_MS) {
        ++s->errors; s->switching=false; s->neutral_required=true;
        return;
    }
    keyboard_report_t empty={0};
    if(!app->sent_valid || memcmp(&app->sent,&empty,sizeof(empty)) ||
       (s->current==M1_TRANSPORT_USB && (app->midi->panic || app->midi->count)) ||
       !s->ops || !s->ops->drained || !s->ops->select || !s->ops->drained(s->ops->context))return;
    if(s->ops->select(s->ops->context,s->target)) {
        s->current=s->target; s->switching=false; s->neutral_required=true;
        app->sent_valid=false; /* new host must get a neutral baseline */
        app->menu->midi_blocked=s->current!=M1_TRANSPORT_USB;
    }
}
