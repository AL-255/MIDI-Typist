#include "defaults.h"
#include "device_store.h"
#include "keyboard_layout.h"
#include <string.h>

uint32_t calibration_crc32(const uint8_t *p, unsigned n)
{
    uint32_t crc=UINT32_MAX;
    while (n--) {
        crc ^= *p++;
        for (unsigned b=0; b<8; ++b) crc=(crc>>1u)^(0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}

enum { SENSOR_OFFSET=33, SENSOR_BYTES=7, CRC_OFFSET=508 };
_Static_assert(SENSOR_OFFSET+CAL_KEYS*SENSOR_BYTES<=CRC_OFFSET,"snapshot exceeds page");
_Static_assert(CAL_SLOT_A==0x78000 && CAL_SLOT_B==0x78200,"review storage addresses before changing");
static uint32_t u32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i) p[i]=v>>(8*i); }
/* Two canonical 1..4096 samples in three bytes; zero represents 1. */
static void pair_put(uint8_t *p,unsigned a,unsigned b)
{ --a; --b; p[0]=a; p[1]=(a>>8)|(b<<4); p[2]=b>>4; }
static unsigned first(const uint8_t *p) { return 1u+(p[0]|(unsigned)(p[1]&15)<<8); }
static unsigned second(const uint8_t *p) { return 1u+((p[1]>>4)|(unsigned)p[2]<<4); }
static bool mapping_valid(unsigned profile,unsigned sensor,unsigned note)
{
    if(note>127 && note!=MIDI_UNMAPPED) return false;
    const uint8_t key=keyboard_key_for_sensor(profile,sensor);
    const keyboard_action_t *a=keyboard_action(profile,key,0);
    const bool control=key==keyboard_layout(profile)->fn || (a && a->type==2 &&
        (a->arg0==1 || a->arg0==4 || a->arg0==8 || a->arg0==16 || a->arg0==64 || a->arg1==0x2c));
    return !control || note==MIDI_UNMAPPED;
}
static void seal(uint8_t *p) { put32(p+CRC_OFFSET,calibration_crc32(p,CRC_OFFSET)); }
bool device_record_valid(const uint8_t *p)
{
    if(memcmp(p,"MTP1",4) || p[4]!=1 || !keyboard_layout_valid(p[5],p[6]) ||
       p[6]>CAL_KEYS || p[7]>1 || u32(p+12)!=0x3150544d ||
       u32(p+CRC_OFFSET)!=calibration_crc32(p,CRC_OFFSET)) return false;
    const uint8_t *g=p+16;
    if(g[0]>1 || g[1]>1 || g[2]>1 || g[3]>19 || g[4]<1 || g[4]>10 ||
       g[5]>11 || g[6]>=MIDI_SCALE_COUNT || (int8_t)g[7]<-MIDI_OCTAVE_LIMIT || (int8_t)g[7]>MIDI_OCTAVE_LIMIT ||
       g[8]>1 || g[9]<1 || g[9]>10 || g[10]<1 || g[10]>10 || g[11]>1 || g[12]>1) return false;
    if(!p[7] && u32(p+29)) return false;
    for(unsigned i=0;i<p[6];++i) {
        const uint8_t *k=p+SENSOR_OFFSET+i*SENSOR_BYTES;
        if(first(k)>=second(k) || second(k)>=4096 || !mapping_valid(p[5],i,k[6])) return false;
        if(p[7] ? second(k+3)<first(k+3)+CALIBRATION_MIN_SPAN_RAW : (k[3] || k[4] || k[5])) return false;
    }
    for(unsigned i=SENSOR_OFFSET+p[6]*SENSOR_BYTES;i<CRC_OFFSET;++i) if(p[i]!=255) return false;
    return true;
}
static void capture(uint8_t *p,const device_store_t *s,const keyboard_app_t *app,
                    const keyboard_calibration_t *cal)
{
    const keyboard_raw_t *r=app->raw; const keyboard_midi_t *m=app->midi;
    const keyboard_config_t *c=&r->engine.config;
    memset(p,255,CAL_PAGE_SIZE); memcpy(p,"MTP1",4); p[4]=1;p[5]=r->profile;p[6]=r->count;
    p[7]=cal!=NULL || s->saved; put32(p+8,s->generation+1u);put32(p+12,0x3150544d);
    const uint8_t globals[]={m->mode,m->janko,m->lower_muted,app->menu->brightness,m->velocity_start,
        m->music.root,m->music.scale,(uint8_t)m->octave,r->enabled,c->saved_actuation,c->saved_rapid,c->rapid_enabled,c->locked};
    memcpy(p+16,globals,sizeof(globals)); put32(p+29,s->calibration_generation+(cal!=NULL));
    for(unsigned i=0;i<r->count;++i) {
        uint8_t *k=p+SENSOR_OFFSET+i*SENSOR_BYTES;
        pair_put(k,r->press[i],r->release[i]); k[6]=m->mapping[i];
        if(cal) pair_put(k+3,cal->lower[i],cal->upper[i]);
        else if(s->saved) memcpy(k+3,s->record+SENSOR_OFFSET+i*SENSOR_BYTES+3,3);
        else memset(k+3,0,3);
    }
}
static void accept(device_store_t *s,const uint8_t *p,unsigned slot)
{
    memcpy(s->record,p,CAL_PAGE_SIZE); s->slot=slot;s->valid=true;s->saved=p[7];
    s->generation=u32(p+8);s->calibration_generation=u32(p+29);
    s->pending=s->cold=s->fault=false;s->error=0;
}
void device_store_load(device_store_t *s,uint8_t profile,uint8_t count,uint16_t *lo,uint16_t *hi,cal_read_fn read)
{
    memset(s,0,sizeof(*s));s->slot=255;s->ready=true;
    uint8_t p[CAL_PAGE_SIZE]; uint32_t fault=0;
    for(unsigned slot=0;slot<2;++slot) {
        const uint32_t error=read(slot,p);
        /* Only invalid content/ECC is recoverable by initializing our owned
         * slots. Timeouts, geometry and other controller faults never erase. */
        if(error && error!=116u) { fault=error; continue; }
        if(error || !device_record_valid(p) || p[5]!=profile || p[6]!=count) continue;
        if(!s->valid || (int32_t)(u32(p+8)-s->generation)>0) accept(s,p,slot);
    }
    s->error=fault;s->fault=fault!=0;
    if(s->saved) for(unsigned i=0;i<count;++i) {
        const uint8_t *k=s->record+SENSOR_OFFSET+i*SENSOR_BYTES+3;
        lo[i]=first(k);hi[i]=second(k);
    }
    s->cold=!s->valid;
}
bool device_store_apply(device_store_t *s,keyboard_app_t *app)
{
    if(!s->ready || s->applied || app->midi->profile!=app->raw->profile) return false;
    s->applied=true;
    if(!s->valid) return false;
    const uint8_t *g=s->record+16;keyboard_raw_t *r=app->raw;keyboard_midi_t *m=app->midi;
    for(unsigned i=0;i<r->count;++i) {
        const uint8_t *k=s->record+SENSOR_OFFSET+i*SENSOR_BYTES;
        r->press[i]=first(k);r->release[i]=second(k);m->mapping[i]=k[6];
    }
    m->mode=g[0];m->janko=g[1];m->lower_muted=g[2];app->menu->brightness=g[3];m->velocity_start=g[4];
    m->music=(midi_music_config_t){g[5],g[6]};m->octave=(int8_t)g[7];r->enabled=g[8];
    r->engine.config.saved_actuation=g[9];r->engine.config.saved_rapid=g[10];
    r->engine.config.rapid_enabled=g[11];r->engine.config.locked=g[12];
    keyboard_raw_invalidate(r);keyboard_midi_abort(m);r->midi_mode=m->mode!=0;
    app->sent_valid=false;
    return true;
}
bool device_store_update(device_store_t *s,keyboard_app_t *app,const keyboard_calibration_t *cal,
                         cal_read_fn read,cal_write_fn write)
{
    uint8_t p[CAL_PAGE_SIZE],verify[CAL_PAGE_SIZE];
    if(s->fault) return false;
    if(cal && (cal->state!=CAL_SAVE || cal->profile!=app->raw->profile || cal->count!=app->raw->count ||
       cal->completed!=cal->count || !calibration_bounds_valid(cal->profile,cal->count,cal->lower,cal->upper))) {
        s->error=0x20001;return false;
    }
    capture(p,s,app,cal);
    seal(p);
    if(!device_record_valid(p)) { s->error=0x20001; return false; }
    unsigned slot=s->slot<2 ? s->slot^1u : 0u;
    /* Never fall back to overwriting the sole good snapshot. The writer only
     * accepts a slot, verifies erase with CMD5, and programs the whole page. */
    s->error=write(slot,p);
    if(!s->error) s->error=read(slot,verify);
    if(!s->error && (memcmp(p,verify,CAL_PAGE_SIZE) || !device_record_valid(verify))) s->error=0x20003;
    if(s->error) { s->fault=true;return false; } /* bounded attempt, no wear/retry loop */
    accept(s,p,slot);return true;
}
bool device_store_service(device_store_t *s,keyboard_app_t *app,uint32_t now,cal_read_fn read,cal_write_fn write)
{
    if(!s->ready || !s->applied || s->fault || app->reset_pending || !app->frame_valid) return false;
    if((uint32_t)(now-s->checked_at)<SETTINGS_CHECK_PERIOD_MS) return false;
    s->checked_at=now;
    uint8_t p[CAL_PAGE_SIZE];capture(p,s,app,NULL);
    /* Exclude generation/checksum: unchanged settings must not wear flash. */
    bool changed=!s->valid || memcmp(p+16,s->record+16,CRC_OFFSET-16);
    if(!changed) {s->pending=false;return false;}
    const uint32_t crc=calibration_crc32(p+16,CRC_OFFSET-16);
    if(!s->pending || crc!=s->pending_crc) {s->pending=true;s->pending_crc=crc;s->changed_at=now;}
    const keyboard_menu_t *menu=app->menu;
    if((uint32_t)(now-s->changed_at)<SETTINGS_SAVE_QUIET_MS || calibration_active(app->cal) ||
       app->raw->engine.config.mode || menu->pending || menu->music_page || menu->press_page ||
       menu->velocity_page || menu->reset_confirmation) return false;
    for(unsigned i=0;i<app->raw->count;++i) if(app->raw->raw[i]<=app->raw->release[i]) return false;
    return device_store_update(s,app,NULL,read,write);
}
bool device_store_clear(device_store_t *s,cal_read_fn read,cal_erase_fn erase)
{
    (void)read;
    /* The user has reserved precisely these two pages for custom state, even
     * if contents/ECC are invalid. erase() must independently blank-verify. */
    const unsigned first_slot=s->slot<2 ? s->slot^1u : 0u;
    for(unsigned i=0;i<2;++i) {
        s->error=erase(first_slot^i);
        if(s->error) { s->fault=true;return false; }
    }
    memset(s,0,sizeof(*s));s->slot=255;return true;
}
