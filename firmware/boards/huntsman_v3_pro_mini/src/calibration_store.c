#include "calibration_store.h"
#include "midi_music.h"
#include <string.h>
#define SET_OFF 276u                             /* after both bound arrays */
#define SET_HDR (4u+1u+1u+4u+SETTINGS_BUILD_MAX) /* magic, version, zero, generation, build */
#define SET_END (SET_OFF+SET_HDR+SETTINGS_PAYLOAD)
_Static_assert(SET_END < CAL_PAGE_SIZE-4u, "settings block must fit the page tail");
static uint16_t get16(const uint8_t *p) { return p[0] | (uint16_t)p[1]<<8u; }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p+2)<<16u; }
static void put16(uint8_t *p, uint16_t v) { p[0]=v; p[1]=v>>8u; }
static void put32(uint8_t *p, uint32_t v) { put16(p,v); put16(p+2,v>>16u); }
uint32_t calibration_crc32(const uint8_t *p, unsigned n)
{
    uint32_t crc=UINT32_MAX;
    while (n--) {
        crc ^= *p++;
        for (unsigned b=0; b<8; ++b) crc=(crc>>1u)^(0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
void calibration_record(uint8_t *p, uint8_t profile, uint8_t count, uint32_t gen, const uint16_t *lo, const uint16_t *hi)
{
    memset(p,255,CAL_PAGE_SIZE); memcpy(p,"HKC1",4); p[4]=1; p[5]=profile; p[6]=count; p[7]=0;
    put32(p+8,gen); put32(p+12,0x314c4143u);
    for (unsigned i=0; i<CAL_KEYS; ++i) { put16(p+16+i*2,i<count?lo[i]:0); put16(p+146+i*2,i<count?hi[i]:0); }
    put32(p+508,calibration_crc32(p,508));
}
static bool owned(const uint8_t *p) { return !memcmp(p,"HKC1",4) && p[4]==1 && p[7]==0 && get32(p+12)==0x314c4143u; }
static bool blank(const uint8_t *p) { for (unsigned i=0; i<CAL_PAGE_SIZE; ++i) if (p[i]!=255) return false; return true; }
/* Calibration part: present (layout 1..3 with matching, valid bounds) or
 * explicitly absent (layout 0, count 0, zeroed arrays) when a page only
 * carries Fn-menu settings. */
static bool calibration_part(const uint8_t *p)
{
    if (p[5]<1 || p[5]>3 || p[6]!=(p[5]==3?65:60+p[5])) return false;
    for (unsigned i=0; i<CAL_KEYS; ++i) {
        unsigned lo=get16(p+16+i*2),hi=get16(p+146+i*2);
        if (i<p[6] ? !lo || hi>4096 || hi<lo+512 : lo || hi) return false;
    }
    return true;
}
static bool calibration_absent(const uint8_t *p)
{
    if (p[5] || p[6]) return false;
    for (unsigned i=0; i<CAL_KEYS; ++i) if (get16(p+16+i*2) || get16(p+146+i*2)) return false;
    return true;
}
static bool settings_absent(const uint8_t *p)
{
    for (unsigned i=SET_OFF; i<CAL_PAGE_SIZE-4u; ++i) if (p[i]!=255) return false;
    return true;
}
/* Payload layout, all unsigned except the signed octave:
 * 0 trigger source, 1 trigger level, 2 rapid level, 3 rapid enabled,
 * 4 MIDI press level (0 = never chosen), 5 velocity start, 6 janko,
 * 7 lower-row mute, 8 brightness, 9 music root, 10 music scale, 11 octave,
 * 12 performance mode, 13..15 reserved zero. */
void device_settings_encode(uint8_t *payload, const device_settings_t *s)
{
    memset(payload,0,SETTINGS_PAYLOAD);
    payload[0]=s->trigger_source; payload[1]=s->trigger_level;
    payload[2]=s->rapid_level; payload[3]=s->rapid_enabled;
    payload[4]=s->midi_press_level; payload[5]=s->velocity_start;
    payload[6]=s->janko; payload[7]=s->lower_muted; payload[8]=s->brightness;
    payload[9]=s->music_root; payload[10]=s->music_scale;
    payload[11]=(uint8_t)s->octave; payload[12]=s->performance_mode;
}
bool device_settings_decode(const uint8_t *payload, device_settings_t *s)
{
    static const uint8_t zero[SETTINGS_PAYLOAD-13u]={0};
    /* A raw trigger point is only meaningful with the level that produced it. */
    const bool raw_source=payload[0]==THRESHOLD_SOURCE_RAW;
    if (memcmp(payload+13,zero,sizeof(zero)) || payload[0]>THRESHOLD_SOURCE_RAW ||
        payload[1]<1u || payload[1]>10u || payload[2]<1u || payload[2]>10u ||
        payload[3]>1u || payload[4]>10u || (raw_source && !payload[4]) ||
        payload[5]<1u || payload[5]>10u || payload[6]>1u || payload[7]>1u ||
        payload[8]>19u || payload[9]>11u || payload[10]>=MIDI_SCALE_COUNT ||
        (int8_t)payload[11]<-10 || (int8_t)payload[11]>10 || payload[12]>1u) return false;
    *s=(device_settings_t){.trigger_source=payload[0],.trigger_level=payload[1],
        .rapid_level=payload[2],.rapid_enabled=payload[3],.midi_press_level=payload[4],
        .velocity_start=payload[5],.janko=payload[6],.lower_muted=payload[7],
        .brightness=payload[8],.music_root=payload[9],.music_scale=payload[10],
        .octave=(int8_t)payload[11],.performance_mode=payload[12]};
    return true;
}
void device_page_settings_put(uint8_t *page, const device_settings_t *s, uint32_t gen, const char *build)
{
    uint8_t *tail=page+SET_OFF;
    memset(tail,255,CAL_PAGE_SIZE-4u-SET_OFF);
    memcpy(tail,"HKS1",4); tail[4]=SETTINGS_VERSION; tail[5]=0; put32(tail+6,gen);
    memset(tail+10,0,SETTINGS_BUILD_MAX);
    for (unsigned i=0; build && build[i] && i<SETTINGS_BUILD_MAX; ++i) tail[10+i]=(uint8_t)build[i];
    device_settings_encode(tail+SET_HDR,s);
    put32(page+CAL_PAGE_SIZE-4u,calibration_crc32(page,CAL_PAGE_SIZE-4u));
}
bool device_page_settings_get(const uint8_t *page, device_settings_t *s, uint32_t *gen, char *build)
{
    const uint8_t *tail=page+SET_OFF;
    if (memcmp(tail,"HKS1",4) || tail[4]!=SETTINGS_VERSION || tail[5]) return false;
    if (!device_settings_decode(tail+SET_HDR,s)) return false;
    if (gen) *gen=get32(tail+6);
    if (build) { memcpy(build,tail+10,SETTINGS_BUILD_MAX); build[SETTINGS_BUILD_MAX-1]=0; }
    for (unsigned i=SET_END; i<CAL_PAGE_SIZE-4u; ++i) if (page[i]!=255) return false;
    return true;
}
/* Both sides are NUL-terminated within the field; the expected identity itself
 * must fit, otherwise it could never have been stored. */
static bool same_build(const char *stored, const char *expect)
{
    if (!expect || strlen(expect)>=SETTINGS_BUILD_MAX) return false;
    for (unsigned i=0;;++i) {
        if (stored[i]!=expect[i]) return false;
        if (!expect[i]) return true;
        if (i+1u>=SETTINGS_BUILD_MAX) return false;
    }
}
/* Calibration load keeps using the stricter calibration_record_valid(); this
 * one also accepts a page that only carries Fn-menu settings. */
bool device_page_valid(const uint8_t *p)
{
    device_settings_t probe;
    if (!owned(p) || get32(p+CAL_PAGE_SIZE-4u)!=calibration_crc32(p,CAL_PAGE_SIZE-4u)) return false;
    if (!calibration_part(p) && !calibration_absent(p)) return false;
    return settings_absent(p) || device_page_settings_get(p,&probe,0,0);
}
bool calibration_record_valid(const uint8_t *p)
{
    device_settings_t probe;
    if (!owned(p) || get32(p+CAL_PAGE_SIZE-4u)!=calibration_crc32(p,CAL_PAGE_SIZE-4u) || !calibration_part(p)) return false;
    return settings_absent(p) || device_page_settings_get(p,&probe,0,0);
}
bool calibration_store_scrub(calibration_store_t *s, cal_read_fn read, cal_erase_fn erase)
{
    uint8_t page[CAL_PAGE_SIZE];
    uint8_t suspect=0u;
    for (unsigned slot=0;slot<2;++slot) {
        /* An unreadable page cannot be classified, so it is reported through the
         * load path's error instead of erasing the region on every boot. */
        if (read(slot,page)) continue;
        if (blank(page) || device_page_valid(page)) continue;
        suspect|=(uint8_t)(1u<<slot);
    }
    if (!suspect) return true;
    if (!calibration_store_clear(s,read,erase)) return false;
    s->corrupt_slots|=suspect;
    s->error=STORE_ERROR_CORRUPT;
    return true;
}
bool calibration_store_clear(calibration_store_t *s, cal_read_fn read, cal_erase_fn erase)
{
    uint8_t page[CAL_PAGE_SIZE];
    bool empty[2], unreadable[2];
    /* Wipe the whole region. Both callers are deliberate deletion requests
     * (Fn+R and `cfg clean`, or the boot integrity pass after a checksum
     * failure), the two pages are ours by verification, and an unreadable page
     * - an interrupted program can leave ECC-invalid data - is erased too,
     * because nothing else can read or reclaim it. Saves stay strict: they
     * skip content they did not write and use the other slot. */
    for (unsigned slot=0;slot<2;++slot) {
        s->error=read(slot,page);
        unreadable[slot]=s->error!=0;
        empty[slot]=!unreadable[slot] && blank(page);
        s->error=0;
    }
    /* Retire the older slot first; never resurrect it if reset is interrupted. */
    unsigned first=s->saved && s->slot<2 ? s->slot^1u : 0u;
    for (unsigned i=0;i<2;++i) {
        unsigned slot=first^i;
        if (!unreadable[slot] && empty[slot]) continue;
        s->error=erase(slot);
        if (s->error) return false;
        s->error=read(slot,page);
        if (s->error) {
            /* The erase itself succeeded, so an unreadable page holds nothing
             * this application could load again: the explicit clear is
             * satisfied even though the read-back stays broken. */
            if (!unreadable[slot]) return false;
            s->error=0;
            continue;
        }
        if (!blank(page)) { s->error=0x20003; return false; }
    }
    *s=(calibration_store_t){.slot=255,.settings_slot=255};
    s->error=0;
    return true;
}
void calibration_store_load(calibration_store_t *s, uint8_t profile, uint8_t count, uint16_t *lo, uint16_t *hi, cal_read_fn read)
{
    /* Calibration part only: the settings fields belong to
     * calibration_store_load_settings, which the caller runs afterwards. */
    s->error=0; s->slot=255u; s->saved=false; s->generation=0;
    uint8_t page[CAL_PAGE_SIZE];
    for (unsigned slot=0; slot<2; ++slot) {
        uint32_t error=read(slot,page);
        if (error) { s->error=error; continue; }
        if (!calibration_record_valid(page) || page[5]!=profile || page[6]!=count) continue;
        uint32_t gen=get32(page+8);
        if (s->saved && (int32_t)(gen-s->generation)<=0) continue;
        s->generation=gen; s->slot=slot; s->saved=true;
        for (unsigned i=0; i<count; ++i) { lo[i]=get16(page+16+i*2); hi[i]=get16(page+146+i*2); }
    }
}
void calibration_store_load_settings(calibration_store_t *s, device_settings_t *out,
                                     const char *expect_build, cal_read_fn read)
{
    uint8_t page[CAL_PAGE_SIZE];
    s->settings_slot=255u; s->settings_saved=false;
    for (unsigned slot=0; slot<2; ++slot) {
        if (read(slot,page)) continue;
        device_settings_t candidate;
        uint32_t gen=0; char build[SETTINGS_BUILD_MAX];
        if (!device_page_settings_get(page,&candidate,&gen,build)) continue;
        /* A record written by another build is the cold-boot condition: its
         * ranges and meanings belong to that build, so start from defaults. */
        if (!same_build(build,expect_build)) continue;
        if (s->settings_saved && (int32_t)(gen-s->settings_generation)<=0) continue;
        *out=candidate; s->settings_generation=gen; s->settings_slot=(uint8_t)slot; s->settings_saved=true;
    }
}
bool calibration_store_save(calibration_store_t *s, const keyboard_calibration_t *cal, cal_read_fn read, cal_write_fn write)
{
    if (cal->state!=CAL_SAVE || cal->completed!=cal->count ||
        !calibration_bounds_valid(cal->profile,cal->count,cal->lower,cal->upper)) { s->error=0x20001; return false; }
    unsigned slot=s->saved ? s->slot^1u : 0u;
    uint8_t page[CAL_PAGE_SIZE], verify[CAL_PAGE_SIZE];
    s->error=read(slot,page);
    if (s->error) return false;
    /* Do not erase unexpected data, even inside the two authorized pages.
     * A recognizable torn HKC1 record can be replaced; unknown damage cannot. */
    if (!blank(page) && !owned(page)) { s->error=0x20002; return false; }
    /* Both parts share the page, so rewriting calibration keeps the Fn-menu
     * settings this page already carries. */
    device_settings_t settings;
    uint32_t settings_gen=0; char build[SETTINGS_BUILD_MAX];
    const bool have_settings=device_page_settings_get(page,&settings,&settings_gen,build);
    calibration_record(page,cal->profile,cal->count,s->generation+1u,cal->lower,cal->upper);
    if (have_settings) device_page_settings_put(page,&settings,settings_gen,build);
    s->error=write(slot,page);
    if (s->error) return false;
    s->error=read(slot,verify);
    if (s->error) return false;
    if (memcmp(page,verify,sizeof(page)) || !device_page_valid(verify)) { s->error=0x20003; return false; }
    s->generation++; s->slot=slot; s->saved=true;
    return true;
}
bool calibration_store_save_settings(calibration_store_t *s, const device_settings_t *in,
                                     const char *build, cal_read_fn read, cal_write_fn write)
{
    uint8_t payload[SETTINGS_PAYLOAD], page[CAL_PAGE_SIZE], verify[CAL_PAGE_SIZE];
    device_settings_t check;
    device_settings_encode(payload,in);
    if (!device_settings_decode(payload,&check)) { s->error=0x20001; return false; }
    const unsigned preferred=s->settings_saved ? s->settings_slot^1u : 0u;
    /* One bad authorized page must not disable persistence: try the preferred
     * slot first and fall back to the other one. */
    for (unsigned attempt=0; attempt<2u; ++attempt) {
        const unsigned slot=attempt ? preferred^1u : preferred;
        s->error=read(slot,page);
        if (s->error) continue;
        if (!blank(page) && !owned(page)) { s->error=0x20002; continue; }
        /* Keep this slot's calibration part verbatim; without one the page
         * carries an explicit empty-calibration header so it stays a
         * recognizable record. */
        const bool have_cal=owned(page) && get32(page+CAL_PAGE_SIZE-4u)==calibration_crc32(page,CAL_PAGE_SIZE-4u) &&
                            calibration_part(page);
        uint8_t keep[SET_OFF];
        if (have_cal) memcpy(keep,page,SET_OFF);
        calibration_record(page,0,0,0,0,0);
        if (have_cal) memcpy(page,keep,SET_OFF);
        device_page_settings_put(page,in,s->settings_generation+1u,build);
        s->error=write(slot,page);
        if (s->error) continue;
        s->error=read(slot,verify);
        if (s->error) continue;
        if (memcmp(page,verify,sizeof(page)) || !device_page_valid(verify)) { s->error=0x20003; continue; }
        s->settings_generation++; s->settings_slot=(uint8_t)slot; s->settings_saved=true;
        return true;
    }
    return false;
}
