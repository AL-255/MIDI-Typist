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
static unsigned fail_reads; /* transient read failures, e.g. an ECC hole */
static uint32_t read_page(unsigned slot, uint8_t *out)
{
    assert(slot<2); memcpy(out,pages[slot],512);
    if (read_error) return read_error;
    if (fail_reads) { --fail_reads; return 116; }
    return 0;
}
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
static device_settings_t sample_settings(void)
{
    const device_settings_t s={.trigger_level=7,.rapid_level=3,.rapid_enabled=1,
        .midi_press_level=9,.velocity_start=6,.janko=1,.lower_muted=1,.brightness=11,
        .music_root=5,.music_scale=2,.octave=-3,.performance_mode=1};
    return s;
}
/* Fn-menu settings share the two authorized pages with calibration: each part
 * keeps its own generation and neither rewrite may destroy the other. */
static void settings_tests(void)
{
    const char *build="v0.1.0-RZ03-0499";
    memset(pages,255,sizeof(pages)); writes=erases=0;
    read_error=write_error=erase_error=0;
    calibration_store_t store; device_settings_t in=sample_settings(),out;
    uint16_t lo[65],hi[65]; unsigned before;
    memset(lo,0,sizeof(lo)); memset(hi,0,sizeof(hi));
    calibration_store_load(&store,1,61,lo,hi,read_page);
    assert(!store.saved);
    /* Cold boot: nothing stored yet, so the application keeps its defaults. */
    calibration_store_load_settings(&store,&out,build,read_page);
    assert(!store.settings_saved && store.settings_slot==255);
    memset(&out,0,sizeof(out));
    assert(calibration_store_save_settings(&store,&in,build,read_page,write_page));
    assert(store.settings_saved && store.settings_slot==0 && store.settings_generation==1 && writes==1);
    assert(!calibration_record_valid(pages[0]));   /* settings-only page */
    calibration_store_load_settings(&store,&out,build,read_page);
    assert(store.settings_saved && !memcmp(&out,&in,sizeof(in)));
    /* Rotation: the next save uses the other slot and reloads newest-first. */
    in.brightness=3;
    assert(calibration_store_save_settings(&store,&in,build,read_page,write_page));
    assert(store.settings_slot==1 && store.settings_generation==2 && writes==2);
    calibration_store_load_settings(&store,&out,build,read_page);
    assert(out.brightness==3);
    /* A calibration save lands in slot 0 and must keep the settings there. */
    keyboard_calibration_t c=capture(1,70000);
    assert(calibration_store_save(&store,&c,read_page,write_page));
    assert(store.slot==0 && store.saved && writes==3);
    assert(calibration_record_valid(pages[0]));
    calibration_store_load_settings(&store,&out,build,read_page);
    assert(out.brightness==3 && out.midi_press_level==9);         /* newest settings kept */
    calibration_store_load(&store,1,61,lo,hi,read_page);
    assert(store.saved && lo[0]==c.lower[0] && hi[0]==c.upper[0]);
    /* A settings save into the calibration slot keeps the calibration part. */
    in.velocity_start=4;
    assert(calibration_store_save_settings(&store,&in,build,read_page,write_page));
    assert(store.settings_slot==0 && store.settings_generation==3 && writes==4);
    assert(calibration_record_valid(pages[0]));
    calibration_store_load(&store,1,61,lo,hi,read_page);
    assert(store.saved && lo[0]==c.lower[0] && hi[0]==c.upper[0]);
    calibration_store_load_settings(&store,&out,build,read_page);
    assert(store.settings_saved && out.velocity_start==4 && out.brightness==3);
    /* Another build's record is the cold-boot condition: nothing loads and the
     * caller keeps the values it already applied. */
    calibration_store_load_settings(&store,&out,"v0.2.0-RZ03-0499",read_page);
    assert(!store.settings_saved && store.settings_slot==255 && out.velocity_start==4);
    calibration_store_load_settings(&store,&out,build,read_page);
    assert(store.settings_saved && store.settings_slot==0 && store.settings_generation==3);
    /* Out-of-range payloads never reach the page. */
    device_settings_t bad=in; bad.velocity_start=0; before=writes;
    assert(!calibration_store_save_settings(&store,&bad,build,read_page,write_page));
    assert(writes==before && store.error==0x20001);
    bad=in; bad.music_scale=200;
    assert(!calibration_store_save_settings(&store,&bad,build,read_page,write_page) && writes==before);
    /* Unknown page contents are never erased, and write faults stay recoverable. */
    uint8_t saved_page[512]; memcpy(saved_page,pages[1],512);
    pages[1][0]=0; uint8_t unknown[512]; memcpy(unknown,pages[1],512); before=writes;
    /* Unknown contents are never erased: the mirror skips that page and uses
     * the other authorized slot instead. */
    assert(calibration_store_save_settings(&store,&in,build,read_page,write_page));
    assert(!memcmp(pages[1],unknown,512) && writes>before);
    memcpy(pages[1],saved_page,512); calibration_store_load_settings(&store,&out,build,read_page);
    write_error=105; assert(!calibration_store_save_settings(&store,&in,build,read_page,write_page));
    write_error=0;
    calibration_store_load_settings(&store,&out,build,read_page);
    assert(store.settings_saved && out.velocity_start==4);
    /* A clear also recovers a page that cannot be read at all: an interrupted
     * program can leave ECC-invalid data that nothing else can reclaim. */
    memcpy(pages[1],saved_page,512); fail_reads=2;
    assert(calibration_store_clear(&store,read_page,erase_page) && erases==2);
    for (unsigned i=0;i<sizeof(pages);++i) assert(((uint8_t *)pages)[i]==255);
    fail_reads=0; memset(pages,255,sizeof(pages)); writes=0; erases=0;
    calibration_store_load(&store,1,61,lo,hi,read_page);
    calibration_store_load_settings(&store,&out,build,read_page);
    assert(calibration_store_save_settings(&store,&in,build,read_page,write_page));
    in.brightness=5;   /* rotate into the other slot so both pages carry content */
    assert(calibration_store_save_settings(&store,&in,build,read_page,write_page));
    /* A dead authorized page never blocks the mirror: the save falls back. */
    memset(pages,255,sizeof(pages)); writes=0; erases=0; fail_reads=1;
    calibration_store_load(&store,1,61,lo,hi,read_page);
    calibration_store_load_settings(&store,&out,build,read_page);
    in.velocity_start=9;
    assert(calibration_store_save_settings(&store,&in,build,read_page,write_page));
    assert(store.settings_slot==0 && store.settings_saved); /* preferred slot unreadable */
    fail_reads=1;   /* the preferred slot stays unreadable on the next save too */
    in.velocity_start=3;
    assert(calibration_store_save_settings(&store,&in,build,read_page,write_page));
    assert(store.settings_slot==0 && store.settings_generation>1);
    fail_reads=0;
    calibration_store_load_settings(&store,&out,build,read_page);
    assert(out.velocity_start==3);
    calibration_store_load(&store,1,61,lo,hi,read_page);
    memcpy(pages[1],pages[0],512); erases=0;   /* both slots populated for the clear below */

    /* Clearing (Fn+R and the flashing script) removes both parts. */
    assert(calibration_store_clear(&store,read_page,erase_page));
    assert(!store.saved && !store.settings_saved && store.settings_slot==255 && erases==2);
    for (unsigned i=0;i<sizeof(pages);++i) assert(((uint8_t *)pages)[i]==255);
    calibration_store_load_settings(&store,&out,build,read_page);
    assert(!store.settings_saved);
    /* Leave the shared fake-flash counters as the calibration body expects. */
    writes=erases=0; read_error=write_error=erase_error=0;
    puts("PASS device settings: cold boot, A/B rotation, calibration/settings coexistence, range and damage guards, clear");
}

/* Corruption handling: the CRC32 covers every byte of the page except itself,
 * so any single-byte change is detected, and a detected tear clears the whole
 * region so the next state is a cold boot rather than a half-applied record. */
static void integrity_tests(void)
{
    const char *build="v0.1.0-RZ03-0499";
    memset(pages,255,sizeof(pages)); writes=erases=0; fail_reads=0;
    read_error=write_error=erase_error=0; erase_bad_verify=false;
    calibration_store_t store; device_settings_t in=sample_settings(),out;
    uint16_t lo[65],hi[65];
    memset(lo,0,sizeof(lo)); memset(hi,0,sizeof(hi));
    calibration_store_load(&store,1,61,lo,hi,read_page);
    in.velocity_start=6;
    assert(calibration_store_save_settings(&store,&in,build,read_page,write_page));
    uint8_t saved[512]; memcpy(saved,pages[0],512);
    /* A clean page passes the pass untouched, with no erase. */
    assert(calibration_store_scrub(&store,read_page,erase_page));
    assert(!store.corrupt_slots && !store.error && !erases && !memcmp(saved,pages[0],512));
    /* Every single-byte change, anywhere in the page, is detected and cleared. */
    for (unsigned offset=0;offset<CAL_PAGE_SIZE;++offset) {
        memset(pages,255,sizeof(pages)); memcpy(pages[0],saved,512);
        pages[0][offset]^=0x55u; erases=0;
        assert(!device_page_valid(pages[0]));
        calibration_store_t probe={0};
        assert(calibration_store_scrub(&probe,read_page,erase_page));
        /* Only the corrupted page holds content, so it is the only erase. */
        assert(probe.corrupt_slots==1u && probe.error==STORE_ERROR_CORRUPT && erases==1);
        for (unsigned i=0;i<sizeof(pages);++i) assert(((uint8_t *)pages)[i]==255);
        calibration_store_load_settings(&probe,&out,build,read_page);
        calibration_store_load(&probe,1,61,lo,hi,read_page);
        assert(!probe.settings_saved && !probe.saved);   /* cold boot */
    }
    /* A power loss during program leaves a prefix of the page written and the
     * rest erased. Every such tear fails the checksum, so a boot clears the
     * region and continues from defaults instead of using a partial record. */
    for (unsigned cut=0;cut<CAL_PAGE_SIZE;cut+=16u) {   /* the full page is not a tear */
        memset(pages,255,sizeof(pages)); memcpy(pages[0],saved,cut); erases=0;
        calibration_store_t torn={0};
        assert(calibration_store_scrub(&torn,read_page,erase_page));
        assert(cut ? (torn.corrupt_slots==1u && torn.error==STORE_ERROR_CORRUPT && erases==1)
                   : (!torn.corrupt_slots && !erases));
        for (unsigned i=0;i<sizeof(pages);++i) assert(((uint8_t *)pages)[i]==255);
        calibration_store_load(&torn,1,61,lo,hi,read_page);
        calibration_store_load_settings(&torn,&out,build,read_page);
        assert(!torn.saved && !torn.settings_saved);
    }
    /* Content that is readable but not a valid record is corruption of the
     * region, so the pass clears it and reports a cold boot. */
    memset(pages,255,sizeof(pages)); pages[0][0]=0x5au; erases=0;
    calibration_store_t probe={0};
    assert(calibration_store_scrub(&probe,read_page,erase_page));
    assert(probe.corrupt_slots==1u && probe.error==STORE_ERROR_CORRUPT && erases==1 && pages[0][0]==0xff);
    /* An unreadable page cannot be classified, so it is left alone. */
    memset(pages,255,sizeof(pages)); memcpy(pages[0],saved,512); fail_reads=2; erases=0;
    calibration_store_t unreadable={0};
    assert(calibration_store_scrub(&unreadable,read_page,erase_page));
    assert(!unreadable.corrupt_slots && !erases && !memcmp(pages[0],saved,512));
    fail_reads=0;
    fail_reads=0; read_error=write_error=erase_error=0; writes=erases=0;
    puts("PASS flash integrity: all 512 single-byte corruptions detected and cleared to a cold boot; unreadable pages reported, not guessed at");
}

int main(void)
{
    settings_tests(); integrity_tests();
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
    /* A clear wipes the whole region, including content we cannot identify:
     * it is a deliberate deletion of the two authorized pages, while a save
     * still skips such a page and uses the other slot. */
    memcpy(pages,saved_pages,sizeof(pages)); erases=0;
    pages[0][0]=0;
    assert(calibration_store_clear(&store,read_page,erase_page) && erases==2);
    erases=0;
    /* An explicit clear recovers a page that cannot be read at all: an
     * interrupted program can leave ECC-invalid data that nothing else can
     * reclaim, and both addresses are authorized pages. */
    memcpy(pages,saved_pages,sizeof(pages)); fail_reads=2;
    assert(calibration_store_clear(&store,read_page,erase_page) && erases==2 && !store.error);
    erases=0; memcpy(pages,saved_pages,sizeof(pages)); read_error=116;
    /* The erase still succeeds, so the clear completes and only the read-back
     * stays broken: nothing loadable survives. */
    assert(calibration_store_clear(&store,read_page,erase_page) && erases==2 && !store.error);
    read_error=0; erases=0; memcpy(pages,saved_pages,sizeof(pages));
    calibration_store_load(&store,1,61,lo,hi,read_page); /* restore the loaded state */
    assert(store.saved);
    erase_error=105;
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
