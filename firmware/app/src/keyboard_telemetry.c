#include "keyboard_telemetry.h"
#include "keyboard_layout.h"
#include "defaults.h"
#include <string.h>

static void u16(uint8_t *p,uint16_t v) { p[0]=v; p[1]=v>>8; }
static void u32(uint8_t *p,uint32_t v) { u16(p,v); u16(p+2,v>>16); }
size_t keyboard_bounds_encode(const keyboard_app_t *app,uint32_t now,
                              uint8_t *out,size_t capacity)
{
    if(!app || !app->raw || !app->cal || !out)return 0;
    unsigned count=app->readback_count;
    if(count>MT_KEY_CAPACITY || (count && !keyboard_layout_valid(app->readback_profile,count)))return 0;
    size_t size=MT_BOUNDS_SIZE(count);
    if(capacity<size)return 0;
    /* Menus can invalidate keyboard output while the sensor frame remains
     * healthy. Readback freshness must not depend on host-output arming. */
    bool fresh=count && app->readback_valid && app->frame_valid &&
        (uint32_t)(now-app->readback_time)<SCAN_STALE_MS;
    memset(out,0,size);memcpy(out,"MTB1",4);out[4]=1;
    out[5]=app->readback_profile;out[6]=count;
    out[7]=(fresh?MT_BOUNDS_VALID:0u) | (app->readback_normalized?MT_BOUNDS_NORMALIZED:0u);
    u32(out+8,app->readback_time);u32(out+12,app->readback_sequence);
    out[16]=app->cal->state;
    for(unsigned i=0;i<count;++i) {
        const keyboard_readback_key_t *key=&app->readback[i];
        uint8_t *p=out+MT_BOUNDS_HEADER_SIZE+i*MT_BOUNDS_RECORD_SIZE;
        u16(p,key->sample);u16(p+2,key->lower);u16(p+4,key->upper);u16(p+6,key->control);
    }
    return size;
}
size_t keyboard_power_encode(const keyboard_power_status_t *s,uint8_t *out,size_t capacity)
{
    if(!s || !out || capacity<MT_POWER_SIZE || s->flags>31u || s->percent>100u ||
       s->charger>MT_CHARGE_RAW_HIGH ||
       (!(s->flags&MT_POWER_VALID) && (s->percent || s->adc!=UINT16_MAX)) ||
       ((s->flags&MT_POWER_EXTERNAL) && !(s->flags&MT_POWER_SOURCE_KNOWN)) ||
       ((s->flags&MT_POWER_LOW) && ((s->flags&7u)!=(MT_POWER_SOURCE_KNOWN|MT_POWER_VALID))) ||
       ((s->flags&MT_POWER_CRITICAL) && !(s->flags&MT_POWER_LOW)) ||
       (s->charger && (!(s->flags&MT_POWER_SOURCE_KNOWN) ||
        ((s->charger==MT_CHARGE_BATTERY)==((s->flags&MT_POWER_EXTERNAL)!=0)))))return 0;
    memcpy(out,"MTP1",4);out[4]=1;out[5]=s->flags;out[6]=s->percent;out[7]=s->charger;
    u16(out+8,s->adc);u16(out+10,0);u32(out+12,s->age_ms);return MT_POWER_SIZE;
}
size_t keyboard_telemetry_encode(const keyboard_app_t *app,
    const keyboard_telemetry_status_t *s,uint8_t *out,size_t capacity)
{
    _Static_assert(sizeof(float)==4,"telemetry requires float32");
    _Static_assert(sizeof(keyboard_report_t)<=MT_GUI_MAX_HID,"HID report exceeds telemetry contract");
    if(!app || !s || !out || !app->raw || !app->midi || !app->cal) return 0;
    const keyboard_raw_t *raw=app->raw;
    const keyboard_midi_t *midi=app->midi;
    const keyboard_calibration_t *cal=app->cal;
    const keyboard_layout_t *layout=keyboard_layout(raw->profile);
    unsigned count=raw->count;
    if(count>MT_GUI_MAX_KEYS || count>MT_KEY_CAPACITY ||
       (count && (!layout || count!=keyboard_layout_count(raw->profile)))) return 0;
    size_t size=MT_GUI_SIZE(count,sizeof(app->sent));
    if(size>capacity) return 0;
    memset(out,0,size);
    memcpy(out,"MTG4",4); u16(out+4,size);
    out[6]=raw->profile; out[7]=count;
    out[8]=raw->enabled | (raw->armed<<1) | (raw->valid<<2) |
           (s->scan_fault<<3) | (s->light_fault<<4) | (midi->janko<<6);
    out[9]=s->result; out[10]=raw->engine.config.mode; out[11]=midi->velocity_start;
    u32(out+12,s->sequence); u32(out+16,raw->revision); u32(out+20,s->ack);
    u32(out+24,s->scan_errors); u32(out+28,s->light_errors);
    u32(out+32,layout?layout->sample_hz:0);
    out[36]=sizeof(app->sent); out[37]=midi->mode; out[38]=(uint8_t)midi->octave;
    out[39]=1; out[40]=midi->panic!=0;
    out[41]=cal->state; out[42]=cal->completed; out[43]=cal->selected;
    out[44]=calibration_active(cal) | (s->calibration_saved<<1) | (s->calibration_supported<<2);
    out[45]=cal->reason; out[46]=s->storage_flags; out[47]=s->storage_slot;
    u32(out+48,midi->errors); u32(out+52,midi->changes);
    uint32_t elapsed=cal->selected<cal->count?(uint32_t)(s->now-cal->holds[cal->selected].since):0;
    uint32_t idle=s->now-cal->activity;
    u16(out+56,cal->state==CAL_COLLECT && cal->selected!=255?
        (elapsed<CALIBRATION_HOLD_MS?elapsed:CALIBRATION_HOLD_MS):0);
    u16(out+58,calibration_active(cal) && idle<CALIBRATION_IDLE_MS?CALIBRATION_IDLE_MS-idle:0);
    if(cal->selected<cal->count) {
        u16(out+60,cal->upper[cal->selected]); u16(out+62,cal->lower[cal->selected]);
    }
    u32(out+64,s->calibration_generation); u32(out+68,s->storage_error);
    u32(out+72,s->storage_generation); u16(out+76,MT_GUI_HEADER_SIZE);
    out[78]=s->transport;out[79]=s->transport_flags;
    for(unsigned i=0;i<count;++i) {
        uint8_t *p=out+MT_GUI_HEADER_SIZE+i*MT_GUI_RECORD_SIZE;
        const keyboard_velocity_t *v=&raw->velocity[i];
        u16(p,raw->raw[i]); u16(p+2,raw->press[i]); u16(p+4,raw->release[i]);
        uint32_t bits; memcpy(&bits,&v->value,4); u32(p+6,bits); u32(p+10,v->captures);
        p[14]=v->ready | (v->valid<<1) | ((v->pending!=0)<<2) |
              ((calibration_active(cal) && cal->holds[i].active)<<3) |
              ((raw->down[i]!=0)<<4) | (((cal->done[i/8]>>(i%8))&1u)<<5);
        p[15]=midi->mapping[i];
        p[16]=raw->keycode[i];
        if(raw->down[i] && keyboard_key_for_sensor(raw->profile,i)==layout->fn) out[8]|=32u;
    }
    memcpy(out+MT_GUI_HEADER_SIZE+count*MT_GUI_RECORD_SIZE,&app->sent,sizeof(app->sent));
    uint32_t checksum=0;
    for(unsigned i=0;i<size-4u;i+=2u) checksum+=out[i]|(uint16_t)out[i+1]<<8;
    u32(out+size-4u,checksum);
    return size;
}
