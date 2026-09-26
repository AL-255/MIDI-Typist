#include "device_store.h"
#include "keyboard_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t pages[2][CAL_PAGE_SIZE];
static unsigned writes,erases,cut=CAL_PAGE_SIZE;
static unsigned erased_slot[2];
static int read_fault=-1;
static uint32_t read_error=111;
static uint32_t read_page(unsigned slot,uint8_t *p)
{ assert(slot<2); if((int)slot==read_fault)return read_error;memcpy(p,pages[slot],CAL_PAGE_SIZE);return 0; }
static uint32_t write_page(unsigned slot,const uint8_t *p)
{ assert(slot<2 && device_record_valid(p));++writes;memset(pages[slot],255,CAL_PAGE_SIZE);memcpy(pages[slot],p,cut);return cut==CAL_PAGE_SIZE?0:105; }
static uint32_t erase_page(unsigned slot)
{ assert(slot<2);erased_slot[erases%2]=slot;++erases;memset(pages[slot],255,CAL_PAGE_SIZE);return 0; }
static keyboard_raw_t raw;
static keyboard_midi_t midi;
static keyboard_menu_t menu;
static keyboard_calibration_t cal;
static keyboard_app_t app;
static uint16_t lo[CAL_KEYS],hi[CAL_KEYS];
static uint8_t expected_code(unsigned profile,unsigned sensor)
{
    if(keyboard_key_for_sensor(profile,sensor)==keyboard_layout(profile)->fn)return 0;
    return sensor%7==0?0:sensor%7==1?0xe7:sensor%7==2?0x87:4+(sensor*13)%228;
}
static void boot(device_store_t *s,unsigned profile)
{
    unsigned count=keyboard_layout_count(profile);
    assert(count && count<=CAL_KEYS);
    keyboard_app_init(&app,&raw,&midi,&menu,&cal,NULL);
    uint16_t samples[CAL_KEYS];
    for(unsigned i=0;i<count;++i){samples[i]=4000;lo[i]=2240;hi[i]=3360;}
    keyboard_app_frame(&app,samples,count,profile,lo,hi,true,10);
    device_store_load(s,profile,count,lo,hi,read_page);
    device_store_apply(s,&app);
}
static void polling_cache(unsigned profile)
{
    device_store_t s;memset(pages,255,sizeof(pages));boot(&s,profile);
    assert(device_store_update(&s,&app,NULL,read_page,write_page));
    unsigned before_writes=writes;uint32_t now=400;
    assert(!device_store_poll(&s,&app,now,true) && s.observed_valid && !s.pending);
    /* Change each persisted input independently. Repeated identical polls
     * must retain the original quiet-window timestamp; reverting an edit
     * must cancel it, even when both configurations have been cached. */
#define CHECK_CHANGE(field,value) do { \
    int previous=(field);(field)=(value);now+=20; \
    assert(!device_store_poll(&s,&app,now,true) && s.pending && !s.fault); \
    uint32_t changed=s.changed_at;now+=20; \
    assert(!device_store_poll(&s,&app,now,true) && s.pending && s.changed_at==changed); \
    (field)=previous;now+=20; \
    assert(!device_store_poll(&s,&app,now,true) && !s.pending && !s.fault); \
} while(0)
    CHECK_CHANGE(midi.mode,!midi.mode);
    CHECK_CHANGE(midi.janko,!midi.janko);
    CHECK_CHANGE(midi.lower_muted,!midi.lower_muted);
    CHECK_CHANGE(menu.brightness,menu.brightness?0:1);
    CHECK_CHANGE(menu.effect,menu.effect==KEYBOARD_LIGHT_WHITE?KEYBOARD_LIGHT_RAINBOW:KEYBOARD_LIGHT_WHITE);
    CHECK_CHANGE(midi.velocity_start,midi.velocity_start==1?2:1);
    CHECK_CHANGE(midi.music.root,midi.music.root?0:1);
    CHECK_CHANGE(midi.music.scale,midi.music.scale?0:1);
    CHECK_CHANGE(midi.octave,midi.octave?0:1);
    CHECK_CHANGE(raw.enabled,!raw.enabled);
    CHECK_CHANGE(raw.engine.config.saved_actuation,raw.engine.config.saved_actuation==1?2:1);
    CHECK_CHANGE(raw.engine.config.saved_rapid,raw.engine.config.saved_rapid==1?2:1);
    CHECK_CHANGE(raw.engine.config.rapid_enabled,!raw.engine.config.rapid_enabled);
    CHECK_CHANGE(raw.engine.config.locked,!raw.engine.config.locked);
    for(unsigned i=0;i<raw.count;++i) {
        CHECK_CHANGE(raw.press[i],raw.press[i]-1);
        CHECK_CHANGE(raw.release[i],raw.release[i]+1);
        if(midi.mapping[i]!=MIDI_UNMAPPED)CHECK_CHANGE(midi.mapping[i],(midi.mapping[i]+1)%128);
        if(keyboard_key_for_sensor(profile,i)!=keyboard_layout(profile)->fn)
            CHECK_CHANGE(raw.keycode[i],raw.keycode[i]?0:4);
    }
#undef CHECK_CHANGE
    assert(writes==before_writes);
    midi.velocity_start=midi.velocity_start==1?2:1;now+=20;
    assert(!device_store_poll(&s,&app,now,true) && s.pending);
    now+=SETTINGS_SAVE_QUIET_MS;
    assert(device_store_poll(&s,&app,now,true));
    assert(device_store_update(&s,&app,NULL,read_page,write_page));
    assert(!s.observed_valid && !s.pending);
    now+=20;assert(!device_store_poll(&s,&app,now,true) && s.observed_valid);
}
int main(void)
{
    unsigned profiles=0;
    for(unsigned profile=1;profile<256;++profile) {
        if(!keyboard_layout_count(profile))continue;
        ++profiles;
        device_store_t s;
        memset(pages,255,sizeof(pages));writes=0;boot(&s,profile);
        assert(s.cold && !s.saved && s.applied && !s.error);
        assert(!device_store_service(&s,&app,100,read_page,write_page));
        assert(device_store_service(&s,&app,360,read_page,write_page));
        assert(s.valid && s.generation==1 && writes==1);
        assert((pages[s.slot][18]&0x70u)==0x70u); /* erased-state White encoding */
        for(unsigned t=400;t<2000;t+=20) assert(!device_store_service(&s,&app,t,read_page,write_page));
        assert(writes==1);
        midi.mode=1;midi.janko=true;midi.lower_muted=true;midi.octave=-3;
        midi.velocity_start=7;midi.music.root=6;midi.music.scale=4;menu.brightness=8;
        menu.effect=KEYBOARD_LIGHT_RAINBOW;
        unsigned mapped=0;while(midi.mapping[mapped]==MIDI_UNMAPPED)++mapped;
        const uint8_t note=midi.mapping[mapped]+5;midi.mapping[mapped]=note;
        for(unsigned i=0;i<raw.count;++i){raw.press[i]=1000+i;raw.release[i]=2000+i;
            raw.keycode[i]=expected_code(profile,i);}
        raw.engine.config.saved_actuation=6;raw.engine.config.saved_rapid=7;
        assert(!device_store_service(&s,&app,2020,read_page,write_page));
        raw.raw[0]=1000;
        assert(!device_store_service(&s,&app,2400,read_page,write_page));
        raw.raw[0]=4000;
        assert(device_store_service(&s,&app,2420,read_page,write_page));
        assert(writes==2 && s.slot==1);
        assert((pages[s.slot][18]&0x70u)==0x60u); /* Rainbow uses former padding */
        boot(&s,profile);
        assert(midi.mode==1 && midi.janko && midi.lower_muted && midi.octave==-3 && menu.brightness==8);
        assert(menu.effect==KEYBOARD_LIGHT_RAINBOW);
        assert(midi.velocity_start==7 && midi.music.root==6 && midi.music.scale==4);
        assert(midi.mapping[mapped]==note);
        assert(raw.engine.config.saved_actuation==6 && raw.engine.config.saved_rapid==7);
        assert(!raw.armed && raw.midi_mode);
        for(unsigned i=0;i<raw.count;++i){assert(raw.press[i]==1000+i && raw.release[i]==2000+i);
            assert(raw.keycode[i]==expected_code(profile,i));}
        cal.state=CAL_SAVE;cal.profile=profile;cal.count=raw.count;cal.completed=raw.count;
        for(unsigned i=0;i<raw.count;++i){cal.lower[i]=1000+i;cal.upper[i]=4000-i;}
        assert(device_store_update(&s,&app,&cal,read_page,write_page));
        assert(s.saved && s.calibration_generation==1 && s.generation==3);
        uint8_t good[CAL_PAGE_SIZE];memcpy(good,pages[0],CAL_PAGE_SIZE);
        /* Reject another format even with an otherwise correct checksum. */
        uint8_t foreign[CAL_PAGE_SIZE];memcpy(foreign,good,CAL_PAGE_SIZE);
        foreign[0]^=1;
        uint32_t crc=calibration_crc32(foreign,CAL_PAGE_SIZE-4);
        for(unsigned byte=0;byte<4;++byte)foreign[CAL_PAGE_SIZE-4+byte]=crc>>(8*byte);
        assert(!device_record_valid(foreign));
        memcpy(foreign,good,CAL_PAGE_SIZE);
        foreign[18]=(foreign[18]&~0x70u)|0x50u; /* unsupported effect ID */
        crc=calibration_crc32(foreign,CAL_PAGE_SIZE-4);
        for(unsigned byte=0;byte<4;++byte)foreign[CAL_PAGE_SIZE-4+byte]=crc>>(8*byte);
        assert(!device_record_valid(foreign));
        for(cut=0;cut<CAL_PAGE_SIZE;++cut) {
            device_store_t attempt=s; midi.velocity_start=9;
            assert(!device_store_update(&attempt,&app,NULL,read_page,write_page));
            assert(attempt.fault && !memcmp(pages[0],good,CAL_PAGE_SIZE));
            boot(&attempt,profile);
            assert(attempt.generation==3 && midi.velocity_start==7 && midi.janko && attempt.saved);
            for(unsigned i=0;i<raw.count;++i)assert(lo[i]==1000+i && hi[i]==4000-i);
            for(unsigned i=0;i<raw.count;++i)assert(raw.keycode[i]==expected_code(profile,i));
        }
        cut=CAL_PAGE_SIZE;
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
        /* NXP's invalid-ECC status is recoverable only on that board. An
         * unrelated controller returning the same number must fail closed. */
        read_error=116;read_fault=1;boot(&s,profile);
        assert(s.valid && s.generation==3);
        assert(s.fault==(MT_STORE_INVALID_READ!=116));
        assert(s.error==(MT_STORE_INVALID_READ==116?0u:116u));
        read_error=111;
        read_fault=-1;
        pages[0][0]^=1;boot(&s,profile);assert(!s.valid && s.cold && !midi.mode && !midi.janko);
        assert(!device_store_service(&s,&app,3000,read_page,write_page));
        assert(device_store_service(&s,&app,3260,read_page,write_page));
        assert(device_record_valid(pages[0]));
        assert(device_store_clear(&s,read_page,erase_page));assert(!s.ready && !s.valid);
    }
    assert(profiles && erases==2*profiles);
    /* Ordered-pair codec: extreme and randomized endpoints, all layouts,
     * every keyboard destination, reserved MIDI roles and full calibration. */
    uint32_t rng=12345;
    for(unsigned profile=1;profile<256;++profile) {
        if(!keyboard_layout_count(profile))continue;
        device_store_t s;memset(pages,255,sizeof(pages));boot(&s,profile);
        for(unsigned round=0;round<64;++round) {
            uint16_t press[CAL_KEYS],release[CAL_KEYS],lower[CAL_KEYS],upper[CAL_KEYS];
            uint8_t codes[CAL_KEYS],notes[CAL_KEYS];
            cal.state=CAL_SAVE;cal.profile=profile;cal.count=raw.count;cal.completed=raw.count;
            for(unsigned i=0;i<raw.count;++i) {
                rng=rng*1664525u+1013904223u;
                release[i]=round==0?2:round==1?4095:2+rng%4094;
                press[i]=round==1?4094:1+(rng>>12)%(release[i]-1);
                upper[i]=round<2?4096:513+rng%3584;
                lower[i]=round==0?1:upper[i]-512;
                unsigned code=(i+round*CAL_KEYS)%229;code=code?code+3:0;
                if(keyboard_key_for_sensor(profile,i)==keyboard_layout(profile)->fn)code=0;
                codes[i]=code;raw.keycode[i]=code;
                notes[i]=midi.mapping[i]==MIDI_UNMAPPED?MIDI_UNMAPPED:(rng>>16)%128;
                midi.mapping[i]=notes[i];
                raw.press[i]=press[i];raw.release[i]=release[i];
                cal.lower[i]=lower[i];cal.upper[i]=upper[i];
            }
            assert(device_store_update(&s,&app,&cal,read_page,write_page));
            boot(&s,profile);
            for(unsigned i=0;i<raw.count;++i) {
                assert(raw.press[i]==press[i] && raw.release[i]==release[i]);
                assert(lo[i]==lower[i] && hi[i]==upper[i]);
                assert(raw.keycode[i]==codes[i] && midi.mapping[i]==notes[i]);
            }
        }
        polling_cache(profile);
    }
    /* RESET retires the older journal slot first. */
    device_store_t saved={.saved=true,.slot=0};
    assert(device_store_clear(&saved,read_page,erase_page));
    assert(erased_slot[0]==1 && erased_slot[1]==0);
    printf("PASS whole-profile boot/defaults, %u layouts, no-change wear, neutral debounce, "
           "settings+calibration, %u torn writes per layout, format rejection, corruption fallback, "
           "fault latch, slot reset\n",profiles,(unsigned)CAL_PAGE_SIZE);
}
