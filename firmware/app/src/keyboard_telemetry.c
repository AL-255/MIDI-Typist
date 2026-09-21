#include "keyboard_telemetry.h"
#include "keyboard_layout.h"
#include <string.h>

/* Offsets are the shared GUI wire contract, not C structure offsets. */
enum {
    RAW=32, PRESS=162, RELEASE=292, DOWN=422, REPORT=431,
    VELOCITY=447, CAPTURES=707, VELOCITY_STATE=967, MIDI=1032, MAPPING=1036,
    MIDI_ERRORS=1104, CALIBRATION=1112, DONE=1120, CAL_BOUNDS=1130,
    CAL_GENERATION=1136, STORE=1144, CHECKSUM=1148
};
_Static_assert(sizeof(keyboard_report_t)==KEYBOARD_TELEMETRY_REPORT_BYTES, "HKG NKRO report contract");
_Static_assert(sizeof(float)==sizeof(uint32_t), "HKG requires float32");
_Static_assert(CHECKSUM+4u==KEYBOARD_TELEMETRY_SIZE, "HKG frame size");

static void le16(uint8_t *p,uint16_t v) { p[0]=v; p[1]=v>>8u; }
static void le32(uint8_t *p,uint32_t v) { le16(p,v); le16(p+2,v>>16u); }

bool keyboard_telemetry_encode(uint8_t out[KEYBOARD_TELEMETRY_SIZE],
    const keyboard_app_t *app,const device_store_t *store,
    const keyboard_telemetry_t *status,uint32_t now)
{
    if(!out || !app || !app->raw || !app->midi || !app->cal || !status) return false;
    const keyboard_raw_t *raw=app->raw;
    const keyboard_midi_t *midi=app->midi;
    const keyboard_calibration_t *cal=app->cal;
    const keyboard_layout_t *layout=keyboard_layout(raw->profile);
    if(raw->count>KEYBOARD_TELEMETRY_KEYS || raw->count>MT_KEY_CAPACITY ||
       (raw->count ? !keyboard_layout_valid(raw->profile,raw->count) : raw->profile!=0u) ||
       cal->count>MT_KEY_CAPACITY || cal->count>KEYBOARD_TELEMETRY_KEYS) return false;
    const bool active=calibration_active(cal);
    memset(out,0,KEYBOARD_TELEMETRY_SIZE);
    memcpy(out,"HKG",3u);
    le16(out+4,KEYBOARD_TELEMETRY_SIZE);
    out[6]=midi->velocity_start;
    out[7]=raw->profile; out[8]=raw->count;
    out[9]=raw->enabled | (raw->armed<<1u) | (raw->valid<<2u) |
        (status->scan_fault<<3u) | (status->light_fault<<4u) | (midi->janko<<6u);
    out[10]=status->result; out[11]=raw->engine.config.mode;
    le32(out+12,status->sequence); le32(out+16,raw->revision);
    le32(out+20,status->ack); le32(out+24,status->scan_errors); le32(out+28,status->light_errors);
    for(unsigned i=0;i<raw->count;++i) {
        le16(out+RAW+i*2u,raw->raw[i]);
        le16(out+PRESS+i*2u,raw->press[i]);
        le16(out+RELEASE+i*2u,raw->release[i]);
        if(raw->down[i]) {
            out[DOWN+i/8u]|=1u<<(i%8u);
            if(keyboard_key_for_sensor(raw->profile,i)==layout->fn) out[9]|=32u;
        }
        const keyboard_velocity_t *v=&raw->velocity[i];
        uint32_t bits;
        memcpy(&bits,&v->value,sizeof(bits));
        le32(out+VELOCITY+i*4u,bits);
        le32(out+CAPTURES+i*4u,v->captures);
        out[VELOCITY_STATE+i]=v->ready | (v->valid<<1u) | ((v->pending!=0u)<<2u);
        if(active && i<cal->count && cal->holds[i].active) out[VELOCITY_STATE+i]|=8u;
        out[MAPPING+i]=midi->mapping[i];
    }
    memcpy(out+REPORT,&app->sent,sizeof(app->sent)); /* last accepted USB submission */
    out[MIDI]=midi->mode; out[MIDI+1]=(uint8_t)midi->octave;
    out[MIDI+2]=1u; out[MIDI+3]=midi->panic!=0u;
    le32(out+MIDI_ERRORS,midi->errors); le32(out+MIDI_ERRORS+4,midi->changes);
    out[CALIBRATION]=cal->state; out[CALIBRATION+1]=cal->completed;
    out[CALIBRATION+2]=cal->selected;
    out[CALIBRATION+3]=active | ((store && store->saved)<<1u) | (status->calibration_supported<<2u);
    uint32_t elapsed=cal->selected<cal->count ? (uint32_t)(now-cal->holds[cal->selected].since) : 0u;
    uint32_t idle=(uint32_t)(now-cal->activity);
    le16(out+CALIBRATION+4,cal->state==CAL_COLLECT && cal->selected<cal->count ?
        (elapsed<CALIBRATION_HOLD_MS ? elapsed : CALIBRATION_HOLD_MS) : 0u);
    le16(out+CALIBRATION+6,active && idle<CALIBRATION_IDLE_MS ? CALIBRATION_IDLE_MS-idle : 0u);
    /* Do not read a fixed nine bytes from a port with a smaller key budget. */
    for(unsigned i=0;i<cal->count;++i)
        if(cal->done[i/8u] & (1u<<(i%8u))) out[DONE+i/8u]|=1u<<(i%8u);
    out[DONE+9]=cal->reason;
    if(cal->selected<cal->count) {
        le16(out+CAL_BOUNDS,cal->upper[cal->selected]); le16(out+CAL_BOUNDS+2,cal->lower[cal->selected]);
    }
    out[STORE+1]=255u;
    if(store) {
        le32(out+CAL_GENERATION,store->calibration_generation); le32(out+CAL_GENERATION+4,store->error);
        out[STORE]=store->valid | (store->pending<<1u) | (store->fault<<2u);
        out[STORE+1]=store->slot; le16(out+STORE+2,(uint16_t)store->generation);
    }
    uint32_t checksum=0;
    for(unsigned i=0;i<CHECKSUM;i+=2u) checksum+=out[i] | (uint16_t)out[i+1]<<8u;
    le32(out+CHECKSUM,checksum);
    return true;
}
