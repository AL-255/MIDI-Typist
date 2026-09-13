#include "calibration_store.h"
#include <string.h>
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
bool calibration_store_clear(calibration_store_t *s, cal_read_fn read, cal_erase_fn erase)
{
    uint8_t page[CAL_PAGE_SIZE];
    bool empty[2];
    /* Check BOTH pages before any erase; never delete unidentified contents. */
    for (unsigned slot=0;slot<2;++slot) {
        s->error=read(slot,page);
        if (s->error) return false;
        empty[slot]=blank(page);
        if (!empty[slot] && !owned(page)) { s->error=0x20002; return false; }
    }
    /* Retire the older slot first; never resurrect it if reset is interrupted. */
    unsigned first=s->saved && s->slot<2 ? s->slot^1u : 0u;
    for (unsigned i=0;i<2;++i) {
        unsigned slot=first^i;
        if (empty[slot]) continue;
        s->error=erase(slot);
        if (s->error) return false;
        s->error=read(slot,page);
        if (s->error) return false;
        if (!blank(page)) { s->error=0x20003; return false; }
    }
    *s=(calibration_store_t){.slot=255};
    return true;
}
bool calibration_record_valid(const uint8_t *p)
{
    if (!owned(p) || p[5]<1 || p[5]>3 || p[6]!=(p[5]==3?65:60+p[5]) ||
        get32(p+508)!=calibration_crc32(p,508)) return false;
    for (unsigned i=0; i<CAL_KEYS; ++i) {
        unsigned lo=get16(p+16+i*2),hi=get16(p+146+i*2);
        if (i<p[6] ? !lo || hi>4096 || hi<lo+512 : lo || hi) return false;
    }
    for (unsigned i=276; i<508; ++i) if (p[i]!=255) return false;
    return true;
}
void calibration_store_load(calibration_store_t *s, uint8_t profile, uint8_t count, uint16_t *lo, uint16_t *hi, cal_read_fn read)
{
    *s=(calibration_store_t){.slot=255};
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
    calibration_record(page,cal->profile,cal->count,s->generation+1u,cal->lower,cal->upper);
    s->error=write(slot,page);
    if (s->error) return false;
    s->error=read(slot,verify);
    if (s->error) return false;
    if (memcmp(page,verify,sizeof(page)) || !calibration_record_valid(verify)) { s->error=0x20003; return false; }
    s->generation++; s->slot=slot; s->saved=true;
    return true;
}
