#include "device_store.h"
#include "keyboard_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t pages[2][512];
static unsigned writes,erases,cut=512;
static unsigned erased_slot[2];
static int read_fault=-1;
static uint32_t read_page(unsigned slot,uint8_t *p)
{ assert(slot<2); if((int)slot==read_fault)return 111;memcpy(p,pages[slot],512);return 0; }
static uint32_t write_page(unsigned slot,const uint8_t *p)
{ assert(slot<2 && device_record_valid(p));++writes;memset(pages[slot],255,512);memcpy(pages[slot],p,cut);return cut==512?0:105; }
static uint32_t erase_page(unsigned slot)
{ assert(slot<2);erased_slot[erases%2]=slot;++erases;memset(pages[slot],255,512);return 0; }
static keyboard_raw_t raw;
static keyboard_midi_t midi;
static keyboard_menu_t menu;
static keyboard_calibration_t cal;
static keyboard_app_t app;
static uint16_t lo[65],hi[65];
static void boot(device_store_t *s,unsigned profile)
{
    unsigned count=keyboard_layout_count(profile);
    keyboard_app_init(&app,&raw,&midi,&menu,&cal,NULL);
    uint16_t samples[65];
    for(unsigned i=0;i<count;++i){samples[i]=4000;lo[i]=2240;hi[i]=3360;}
    keyboard_app_frame(&app,samples,count,profile,lo,hi,true,10);
    device_store_load(s,profile,count,lo,hi,read_page);
    device_store_apply(s,&app);
}
int main(void)
{
    unsigned tested=0;
    for(unsigned profile=1;profile<256;++profile) {
        if(!keyboard_layout(profile))continue;
        ++tested;
        device_store_t s;
        memset(pages,255,sizeof(pages));writes=0;boot(&s,profile);
        assert(s.cold && !s.saved && s.applied && !s.error);
        assert(!device_store_service(&s,&app,100,read_page,write_page));
        assert(device_store_service(&s,&app,360,read_page,write_page));
        assert(s.valid && s.generation==1 && writes==1);
        for(unsigned t=400;t<2000;t+=20) assert(!device_store_service(&s,&app,t,read_page,write_page));
        assert(writes==1);
        midi.mode=1;midi.janko=true;midi.lower_muted=true;midi.octave=-3;
        midi.velocity_start=7;midi.music.root=6;midi.music.scale=4;menu.brightness=8;
        unsigned mapped=0;while(midi.mapping[mapped]==MIDI_UNMAPPED)++mapped;
        const uint8_t note=midi.mapping[mapped]+5;midi.mapping[mapped]=note;
        for(unsigned i=0;i<raw.count;++i){raw.press[i]=1000+i;raw.release[i]=2000+i;}
        raw.engine.config.saved_actuation=6;raw.engine.config.saved_rapid=7;
        assert(!device_store_service(&s,&app,2020,read_page,write_page));
        raw.raw[0]=1000;
        assert(!device_store_service(&s,&app,2400,read_page,write_page));
        raw.raw[0]=4000;
        assert(device_store_service(&s,&app,2420,read_page,write_page));
        assert(writes==2 && s.slot==1);
        boot(&s,profile);
        assert(midi.mode==1 && midi.janko && midi.lower_muted && midi.octave==-3 && menu.brightness==8);
        assert(midi.velocity_start==7 && midi.music.root==6 && midi.music.scale==4);
        assert(midi.mapping[mapped]==note);
        assert(raw.engine.config.saved_actuation==6 && raw.engine.config.saved_rapid==7);
        assert(!raw.armed && raw.midi_mode);
        for(unsigned i=0;i<raw.count;++i)assert(raw.press[i]==1000+i && raw.release[i]==2000+i);
        cal.state=CAL_SAVE;cal.profile=profile;cal.count=raw.count;cal.completed=raw.count;
        for(unsigned i=0;i<raw.count;++i){cal.lower[i]=1000+i;cal.upper[i]=4000-i;}
        assert(device_store_update(&s,&app,&cal,read_page,write_page));
        assert(s.saved && s.calibration_generation==1 && s.generation==3);
        uint8_t good[512];memcpy(good,pages[0],512);
        for(cut=0;cut<512;++cut) {
            device_store_t attempt=s; midi.velocity_start=9;
            assert(!device_store_update(&attempt,&app,NULL,read_page,write_page));
            assert(attempt.fault && !memcmp(pages[0],good,512));
            boot(&attempt,profile);
            assert(attempt.generation==3 && midi.velocity_start==7 && midi.janko && attempt.saved);
            for(unsigned i=0;i<raw.count;++i)assert(lo[i]==1000+i && hi[i]==4000-i);
        }
        cut=512;
        midi.velocity_start=9;
        assert(device_store_update(&s,&app,NULL,read_page,write_page));
        boot(&s,profile);assert(midi.velocity_start==9 && s.calibration_generation==1);
        /* CRC corruption falls back to the other complete generation. */
        pages[1][20]^=1;boot(&s,profile);assert(midi.velocity_start==7 && s.generation==3);
        /* Any controller fault inhibits writing, regardless of slot order. */
        for(read_fault=0;read_fault<2;++read_fault) {
            boot(&s,profile);assert(s.fault && s.error==111);
            assert(!device_store_service(&s,&app,3000,read_page,write_page));
        }
        read_fault=-1;
        pages[0][0]^=1;boot(&s,profile);assert(!s.valid && s.cold && !midi.mode && !midi.janko);
        assert(!device_store_service(&s,&app,3000,read_page,write_page));
        assert(device_store_service(&s,&app,3260,read_page,write_page));
        assert(device_record_valid(pages[0]));
        assert(device_store_clear(&s,read_page,erase_page));assert(!s.ready && !s.valid);
    }
    assert(tested && erases==2*tested);
    /* RESET retires the older journal slot first. */
    device_store_t saved={.saved=true,.slot=0};
    assert(device_store_clear(&saved,read_page,erase_page));
    assert(erased_slot[0]==1 && erased_slot[1]==0);
    puts("PASS whole-profile boot/defaults, all layouts, no-change wear, neutral debounce, settings+calibration, 512 torn writes, corruption fallback, fault latch, tail reset");
}
