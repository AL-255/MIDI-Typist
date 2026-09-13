#include "calibration_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t pages[2][512];
static unsigned writes, cut=512;
static uint32_t read_error, write_error;
static unsigned erases, erase_order[2];
static uint32_t erase_error;
static bool erase_bad_verify;
static unsigned erase_stop_slot, erase_prefix;
static uint32_t interrupted_erase(unsigned slot)
{
    assert(slot<2);
    memset(pages[slot],255,slot==erase_stop_slot ? erase_prefix : 512);
    return slot==erase_stop_slot ? 105 : 0;
}
static uint32_t erase_page(unsigned slot)
{
    assert(slot<2 && erases<2); erase_order[erases++]=slot;
    if (erase_error) return erase_error;
    memset(pages[slot],255,512);
    if (erase_bad_verify) pages[slot][0]=0;
    return 0;
}
static uint32_t read_page(unsigned slot, uint8_t *out) { assert(slot<2); memcpy(out,pages[slot],512); return read_error; }
static uint32_t write_page(unsigned slot, const uint8_t *in)
{
    assert(slot<2); ++writes;
    if (write_error) return write_error;
    memset(pages[slot],255,512); memcpy(pages[slot],in,cut);
    return cut==512 ? 0 : 105;
}
static void fill(uint16_t *raw, unsigned n, unsigned value) { for (unsigned i=0;i<n;++i) raw[i]=value; }
static keyboard_calibration_t capture(unsigned profile, uint32_t now)
{
    keyboard_calibration_t c; calibration_init(&c);
    unsigned n=profile==3 ? 65 : 60+profile;
    uint16_t raw[65]; fill(raw,n,4000);
    assert(calibration_start(&c,profile,n,now));
    assert(!calibration_start(&c,profile,n,now));
    calibration_frame(&c,raw,true,false,now+100);
    assert(c.state==CAL_RELEASE);
    calibration_frame(&c,raw,true,true,now+200);
    calibration_frame(&c,raw,true,true,now+699);
    assert(c.state==CAL_SETTLE);
    calibration_frame(&c,raw,true,true,now+700);
    assert(c.state==CAL_COLLECT);
    now+=701;
    for (unsigned i=0;i<n;++i) {
        raw[i]=1000+i;
        calibration_frame(&c,raw,true,false,now);
        assert(c.selected==i);
        calibration_frame(&c,raw,true,false,now+999);
        assert(c.completed==i);
        calibration_frame(&c,raw,true,false,now+1000);
        assert(c.completed==i+1 && c.lower[i]==1000+i && c.upper[i]==4000);
        raw[i]=4000;
        calibration_frame(&c,raw,true,true,now+1001);
        now+=1100;
    }
    assert(c.state==CAL_SAVE && calibration_bounds_valid(profile,n,c.lower,c.upper));
    return c;
}
static void parallel(unsigned profile, uint32_t now)
{
    keyboard_calibration_t c; calibration_init(&c);
    unsigned n=profile==3 ? 65 : 60+profile;
    uint16_t raw[65]; fill(raw,n,4000);
    assert(calibration_start(&c,profile,n,now));
    calibration_frame(&c,raw,true,true,now);
    calibration_frame(&c,raw,true,true,now+500);
    for (unsigned i=0;i<n;++i) raw[i]=1000+i;
    calibration_frame(&c,raw,true,false,now+501);
    for (unsigned i=0;i<n;++i) assert(c.holds[i].active);
    /* Full 8 kHz input, one independent mean per sensor. */
    uint32_t sums[65]; for (unsigned i=0;i<n;++i) sums[i]=raw[i];
    for (unsigned sample=1;sample<=8000;++sample) {
        for (unsigned i=0;i<n;++i) { raw[i]=1000+i+sample%3; sums[i]+=raw[i]; }
        calibration_frame(&c,raw,true,false,now+501+sample/8);
        if (sample<8000) assert(!c.completed);
    }
    assert(c.state==CAL_SAVE && c.completed==n && c.selected==255);
    for (unsigned i=0;i<n;++i) {
        assert(!c.holds[i].active && c.lower[i]==(sums[i]+4000)/8001);
    }
}
int main(void)
{
    keyboard_calibration_t c=capture(1,0);
    (void)capture(2,0); (void)capture(3,UINT32_MAX-500);
    parallel(1,0); parallel(2,10000); parallel(3,UINT32_MAX-700);
    keyboard_calibration_t s; calibration_init(&s);
    assert(!calibration_start(&s,1,62,0));
    assert(calibration_start(&s,1,61,UINT32_MAX-200));
    calibration_tick(&s,true,4798); assert(calibration_active(&s));
    calibration_tick(&s,true,4799); assert(s.state==CAL_ABORTED && s.reason==CAL_TIMEOUT);
    assert(calibration_start(&s,1,61,0));
    uint16_t raw[65]; fill(raw,65,4000);
    calibration_frame(&s,raw,true,true,0); calibration_frame(&s,raw,true,true,500);
    raw[0]=1000; raw[1]=1100; raw[2]=1200;
    calibration_frame(&s,raw,true,false,501);
    assert(s.selected==0 && !s.completed && s.holds[0].active && s.holds[1].active && s.holds[2].active);
    raw[2]=4000; calibration_frame(&s,raw,true,false,502);
    assert(!s.holds[2].active && s.holds[1].active);
    raw[0]=1500; calibration_frame(&s,raw,true,false,1000);
    calibration_frame(&s,raw,true,false,1502); assert(s.completed==1 && s.lower[1]==1100 && s.holds[0].active);
    calibration_frame(&s,raw,true,false,2000); assert(s.completed==2 && s.lower[0]==1500);
    /* Keep completed keys held while another starts and completes. */
    raw[2]=1250; calibration_frame(&s,raw,true,false,2001);
    calibration_frame(&s,raw,true,false,3000); assert(s.completed==2);
    calibration_frame(&s,raw,true,false,3001); assert(s.completed==3 && s.lower[2]==1250);
    calibration_abort(&s,CAL_CANCELLED,3001); assert(!s.completed && !s.upper[0] && !s.lower[0]);
    for (unsigned i=0;i<65;++i) assert(!s.holds[i].active && !s.holds[i].samples);
    assert(calibration_start(&s,1,61,0)); raw[2]=0;
    calibration_frame(&s,raw,true,true,0); assert(s.reason==CAL_INVALID);

    calibration_store_t store;
    uint16_t lo[65],hi[65]; fill(lo,65,2240); fill(hi,65,3360);
    memset(pages,255,sizeof(pages));
    calibration_store_load(&store,1,61,lo,hi,read_page);
    assert(!store.saved && lo[0]==2240 && !writes);
    assert(calibration_store_save(&store,&c,read_page,write_page));
    assert(writes==1 && store.slot==0 && store.generation==1);
    uint8_t first[512]; memcpy(first,pages[0],512);
    c.lower[0]=1100;
    for (cut=0;cut<512;++cut) {
        calibration_store_t attempt=store;
        assert(!calibration_store_save(&attempt,&c,read_page,write_page));
        assert(!memcmp(first,pages[0],512));
        calibration_store_t reboot;
        calibration_store_load(&reboot,1,61,lo,hi,read_page);
        assert(reboot.saved && reboot.generation==1 && lo[0]==1000);
        memset(pages[1],255,512);
    }
    cut=512;
    pages[1][200]=0x42;
    unsigned before=writes;
    assert(!calibration_store_save(&store,&c,read_page,write_page));
    assert(store.error==0x20002 && before==writes && pages[1][200]==0x42);
    memset(pages[1],255,512); read_error=116;
    assert(!calibration_store_save(&store,&c,read_page,write_page) && writes==before);
    read_error=0; write_error=105;
    assert(!calibration_store_save(&store,&c,read_page,write_page) && store.generation==1);
    write_error=0;
    assert(calibration_store_save(&store,&c,read_page,write_page));
    assert(store.slot==1 && store.generation==2 && !memcmp(first,pages[0],512));
    calibration_store_load(&store,1,61,lo,hi,read_page);
    assert(store.generation==2 && lo[0]==1100);
    calibration_store_load(&store,2,62,lo,hi,read_page); assert(!store.saved);
    for (unsigned i=0;i<512;++i) {
        uint8_t tmp[512]; memcpy(tmp,first,512); tmp[i]^=1;
        assert(!calibration_record_valid(tmp));
    }
    calibration_record(pages[0],1,61,UINT32_MAX,c.lower,c.upper);
    calibration_record(pages[1],1,61,0,c.lower,c.upper);
    calibration_store_load(&store,1,61,lo,hi,read_page); assert(store.slot==1 && !store.generation);
    uint8_t saved_pages[2][512]; memcpy(saved_pages,pages,sizeof(pages));
    for (erase_stop_slot=0;erase_stop_slot<2;++erase_stop_slot)
        for (erase_prefix=0;erase_prefix<=512;++erase_prefix) {
            memcpy(pages,saved_pages,sizeof(pages));
            calibration_store_t attempt=store, reboot;
            assert(!calibration_store_clear(&attempt,read_page,interrupted_erase));
            calibration_store_load(&reboot,1,61,lo,hi,read_page);
            assert(!reboot.saved || reboot.generation==0); /* never the older UINT32_MAX record */
        }
    memcpy(pages,saved_pages,sizeof(pages));
    pages[0][0]=0;
    assert(!calibration_store_clear(&store,read_page,erase_page) && !erases && store.error==0x20002);
    memcpy(pages,saved_pages,sizeof(pages)); read_error=116;
    assert(!calibration_store_clear(&store,read_page,erase_page) && !erases);
    read_error=0; erase_error=105;
    assert(!calibration_store_clear(&store,read_page,erase_page) && erases==1 && store.saved);
    erases=0; erase_error=0; erase_bad_verify=true;
    assert(!calibration_store_clear(&store,read_page,erase_page) && erases==1 && store.error==0x20003);
    erases=0; erase_bad_verify=false; memcpy(pages,saved_pages,sizeof(pages));
    assert(calibration_store_clear(&store,read_page,erase_page));
    assert(erases==2 && erase_order[0]==0 && erase_order[1]==1 && !store.saved && !store.generation);
    for (unsigned i=0;i<sizeof(pages);++i) assert(((uint8_t *)pages)[i]==255);
    erases=0; assert(calibration_store_clear(&store,read_page,erase_page) && !erases);
    calibration_store_load(&store,1,61,lo,hi,read_page); assert(!store.saved);
    c.completed=60; before=writes;
    assert(!calibration_store_save(&store,&c,read_page,write_page) && writes==before);
    c.completed=61; calibration_finish(&c,true,70000); assert(c.state==CAL_DONE);
    puts("PASS calibration timing/layouts/wrap/noise/cancel, CRC, A/B reload, all 512 torn-write cut points, unknown-page and error guards");
}
