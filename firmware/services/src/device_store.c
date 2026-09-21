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

enum { GLOBAL_OFFSET=14, SENSOR_OFFSET=19, CRC_OFFSET=CAL_PAGE_SIZE-4, GENERATION_OFFSET=6, CAL_GENERATION_OFFSET=10 };
_Static_assert(CAL_PAGE_SIZE>SENSOR_OFFSET+4,"snapshot header capacity");
_Static_assert(sizeof(MT_STORE_MAGIC)==5,"snapshot identity must contain four bytes");
_Static_assert(MIDI_SCALE_COUNT<=16 && MIDI_OCTAVE_LIMIT<=10,"snapshot global field capacity");
static uint32_t u32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i) p[i]=v>>(8*i); }
static uint32_t getbits(const uint8_t *p,unsigned *position,unsigned count)
{
    uint32_t value=0; unsigned shift=0;
    while(count) {
        unsigned offset=*position&7u,n=8u-offset;
        if(n>count)n=count;
        value|=((p[*position/8u]>>offset)&((1u<<n)-1u))<<shift;
        *position+=n; count-=n; shift+=n;
    }
    return value;
}
static void putbits(uint8_t *p,unsigned *position,unsigned count,uint32_t value)
{
    while(count) {
        unsigned offset=*position&7u,n=8u-offset;
        if(n>count)n=count;
        unsigned mask=((1u<<n)-1u)<<offset;
        p[*position/8u]=(p[*position/8u]&~mask)|((value<<offset)&mask);
        *position+=n; count-=n; value>>=n;
    }
}
/* Rank the ordered pair 1 <= a < b <= 4096. There are fewer than 2^23
 * possible pairs; this saves a bit without quantizing either endpoint. */
static uint32_t pair_code(unsigned a,unsigned b)
{ return (b-1u)*(b-2u)/2u+a-1u; }
static bool pair_decode(uint32_t code,unsigned *a,unsigned *b)
{
    if(code>=4096u*4095u/2u)return false;
    unsigned lo=2,hi=4096;
    while(lo<hi) {
        unsigned mid=(lo+hi+1u)/2u;
        if((mid-1u)*(mid-2u)/2u<=code)lo=mid;else hi=mid-1u;
    }
    *b=lo; *a=code-(lo-1u)*(lo-2u)/2u+1u;
    return true;
}
/* Physical MIDI roles stay reserved even if their keyboard output is remapped. */
static unsigned role(unsigned profile,unsigned sensor)
{
    const uint8_t key=keyboard_key_for_sensor(profile,sensor);
    if(key==keyboard_layout(profile)->fn)return 0;
    const keyboard_action_t *a=keyboard_action(profile,key,0);
    return a && a->type==2 && (a->arg0==1 || a->arg0==4 || a->arg0==8 ||
        a->arg0==16 || a->arg0==64 || a->arg1==0x2c)?1u:2u;
}
static unsigned map_bits(unsigned role) { return role==0?0u:role==1?8u:15u; }
static unsigned sensor_end(unsigned profile,unsigned count)
{
    unsigned bits=SENSOR_OFFSET*8u;
    for(unsigned i=0;i<count;++i)bits+=46u+map_bits(role(profile,i));
    return bits;
}
static const uint8_t global_widths[]={1,1,1,5,4,4,4,5,1,4,4,1,1};
static void globals_get(const uint8_t *p,uint8_t g[13])
{
    unsigned pos=GLOBAL_OFFSET*8u;
    for(unsigned i=0;i<13;++i)g[i]=getbits(p,&pos,global_widths[i]);
    g[7]=(uint8_t)(g[7]-10);
}
static bool sensor_get(const uint8_t *p,unsigned *pos,unsigned r,
                       unsigned *press,unsigned *release,unsigned *lower,unsigned *upper,
                       unsigned *note,unsigned *keycode)
{
    uint32_t thresholds=getbits(p,pos,23),calibration=getbits(p,pos,23);
    unsigned mapping=getbits(p,pos,map_bits(r));
    if(!pair_decode(thresholds,press,release) || *release>=4096)return false;
    if(p[5]) {
        if(!pair_decode(calibration,lower,upper) || *upper<*lower+CALIBRATION_MIN_SPAN_RAW)return false;
    } else {
        if(calibration)return false;
        *lower=*upper=0;
    }
    *note=MIDI_UNMAPPED; *keycode=0;
    if(r==1)*keycode=mapping;
    else if(r==2) {
        if(mapping>=229u*129u)return false;
        unsigned index=mapping/129u;
        *keycode=index?index+3u:0;
        *note=mapping%129u;
        if(*note==128)*note=MIDI_UNMAPPED;
    }
    return keyboard_keycode_valid(*keycode);
}
static void seal(uint8_t *p) { put32(p+CRC_OFFSET,calibration_crc32(p,CRC_OFFSET)); }
bool device_record_valid(const uint8_t *p)
{
    unsigned count=keyboard_layout_count(p[4]);
    if(memcmp(p,MT_STORE_MAGIC,4) || !count || count>CAL_KEYS || p[5]>1 ||
       sensor_end(p[4],count)>CRC_OFFSET*8u ||
       u32(p+CRC_OFFSET)!=calibration_crc32(p,CRC_OFFSET))return false;
    uint8_t g[13];globals_get(p,g);
    if(g[3]>19 || g[4]<1 || g[4]>10 || g[5]>11 || g[6]>=MIDI_SCALE_COUNT ||
       (int8_t)g[7]<-MIDI_OCTAVE_LIMIT || (int8_t)g[7]>MIDI_OCTAVE_LIMIT ||
       g[9]<1 || g[9]>10 || g[10]<1 || g[10]>10 || (p[18]&0xf0u)!=0xf0u)return false;
    if(!p[5] && u32(p+CAL_GENERATION_OFFSET))return false;
    unsigned pos=SENSOR_OFFSET*8u;
    for(unsigned i=0;i<count;++i) {
        unsigned press,release,lower,upper,note,keycode;
        if(!sensor_get(p,&pos,role(p[4],i),&press,&release,&lower,&upper,&note,&keycode))return false;
    }
    while(pos<CRC_OFFSET*8u)if(!getbits(p,&pos,1))return false;
    return true;
}
static bool capture(uint8_t *p,const device_store_t *s,const keyboard_app_t *app,
                    const keyboard_calibration_t *cal)
{
    const keyboard_raw_t *r=app->raw; const keyboard_midi_t *m=app->midi;
    const keyboard_config_t *c=&r->engine.config;
    if(!keyboard_layout_valid(r->profile,r->count) || sensor_end(r->profile,r->count)>CRC_OFFSET*8u)
        return false;
    memset(p,255,CAL_PAGE_SIZE); memcpy(p,MT_STORE_MAGIC,4);p[4]=r->profile;p[5]=cal!=NULL || s->saved;
    put32(p+GENERATION_OFFSET,s->generation+1u);
    put32(p+CAL_GENERATION_OFFSET,s->calibration_generation+(cal!=NULL));
    const uint8_t globals[]={m->mode,m->janko,m->lower_muted,app->menu->brightness,m->velocity_start,
        m->music.root,m->music.scale,(uint8_t)(m->octave+10),r->enabled,
        c->saved_actuation,c->saved_rapid,c->rapid_enabled,c->locked};
    unsigned pos=GLOBAL_OFFSET*8u;
    for(unsigned i=0;i<13;++i) {
        if(globals[i]>=(1u<<global_widths[i]))return false;
        putbits(p,&pos,global_widths[i],globals[i]);
    }
    pos=SENSOR_OFFSET*8u;
    unsigned previous=pos;
    for(unsigned i=0;i<r->count;++i) {
        unsigned kind=role(r->profile,i),usage=r->keycode[i],note=m->mapping[i];
        if(!keyboard_keycode_valid(usage) || (note>127 && note!=MIDI_UNMAPPED) ||
           (kind<2 && note!=MIDI_UNMAPPED) || (!kind && usage) ||
           !r->press[i] || r->press[i]>=r->release[i] || r->release[i]>=4096)return false;
        putbits(p,&pos,23,pair_code(r->press[i],r->release[i]));
        previous+=23;
        uint32_t calibration=s->saved?getbits(s->record,&previous,23):0;
        if(!s->saved)previous+=23;
        previous+=map_bits(kind);
        if(cal)calibration=pair_code(cal->lower[i],cal->upper[i]);
        putbits(p,&pos,23,calibration);
        unsigned mapping=kind==1?usage:(usage?usage-3u:0)*129u+(note==MIDI_UNMAPPED?128u:note);
        putbits(p,&pos,map_bits(kind),mapping);
    }
    return true;
}
static void accept(device_store_t *s,const uint8_t *p,unsigned slot)
{
    memcpy(s->record,p,CAL_PAGE_SIZE); s->slot=slot;s->valid=true;s->saved=p[5];
    s->generation=u32(p+GENERATION_OFFSET);s->calibration_generation=u32(p+CAL_GENERATION_OFFSET);
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
        if(error && error!=MT_STORE_INVALID_READ) { fault=error; continue; }
        if(error || !device_record_valid(p) || p[4]!=profile || keyboard_layout_count(p[4])!=count) continue;
        if(!s->valid || (int32_t)(u32(p+GENERATION_OFFSET)-s->generation)>0) accept(s,p,slot);
    }
    s->error=fault;s->fault=fault!=0;
    if(s->saved) {
        unsigned pos=SENSOR_OFFSET*8u;
        for(unsigned i=0;i<count;++i) {
            unsigned press,release,lower,upper,note,keycode;
            (void)sensor_get(s->record,&pos,role(profile,i),&press,&release,&lower,&upper,&note,&keycode);
            lo[i]=lower;hi[i]=upper;
        }
    }
    s->cold=!s->valid;
}
bool device_store_apply(device_store_t *s,keyboard_app_t *app)
{
    if(!s->ready || s->applied || app->midi->profile!=app->raw->profile) return false;
    s->applied=true;
    if(!s->valid) return false;
    uint8_t g[13];globals_get(s->record,g);
    keyboard_raw_t *r=app->raw;keyboard_midi_t *m=app->midi;
    unsigned pos=SENSOR_OFFSET*8u;
    for(unsigned i=0;i<r->count;++i) {
        unsigned press,release,lower,upper,note,keycode;
        (void)sensor_get(s->record,&pos,role(r->profile,i),&press,&release,&lower,&upper,&note,&keycode);
        r->press[i]=press;r->release[i]=release;m->mapping[i]=note;r->keycode[i]=keycode;
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
    if(!capture(p,s,app,cal)) { s->error=0x20001;return false; }
    seal(p);
    if(!device_record_valid(p)) { s->error=0x20001; return false; }
    unsigned slot=s->slot<2 ? s->slot^1u : 0u;
    /* Never fall back to overwriting the sole good snapshot. The board writer
     * must restrict the slot, blank-verify erase and program the whole page. */
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
    uint8_t p[CAL_PAGE_SIZE];
    if(!capture(p,s,app,NULL)) { s->error=0x20001;s->fault=true;return false; }
    /* Exclude generation/checksum: unchanged settings must not wear flash. */
    bool changed=!s->valid || memcmp(p+GLOBAL_OFFSET,s->record+GLOBAL_OFFSET,CRC_OFFSET-GLOBAL_OFFSET);
    if(!changed) {s->pending=false;return false;}
    const uint32_t crc=calibration_crc32(p+GLOBAL_OFFSET,CRC_OFFSET-GLOBAL_OFFSET);
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
    /* The board must prove ownership of both slots, including when their
     * contents are invalid. erase() must independently blank-verify. */
    const unsigned first_slot=s->slot<2 ? s->slot^1u : 0u;
    for(unsigned i=0;i<2;++i) {
        s->error=erase(first_slot^i);
        if(s->error) { s->fault=true;return false; }
    }
    memset(s,0,sizeof(*s));s->slot=255;return true;
}
