#include "m1_board.h"
#include "m1_lighting.h"
#include "m1_battery.h"
#include "m1_controls.h"
#include "m1_power.h"
#include "m1_radio.h"
#include "m1_radio_keyboard.h"
#include "m1_wake.h"
#include "m1_factory.h"
#include "keyboard_app.h"
#include "keyboard_layout.h"
#include "keyboard_lighting.h"
#include "defaults.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void mapping(void)
{
    assert(keyboard_layout_count(M1_PROFILE)==82);
    assert(!keyboard_layout(2));
    bool cells[126]={false}, leds[M1_KEY_COUNT]={false};
    for(unsigned i=0;i<M1_KEY_COUNT;++i) {
        unsigned cell=m1_factory_cell(i),led=m1_led_index(i);
        assert(cell<90 && !cells[cell]); cells[cell]=true;
        assert(led<82 && !leds[led]); leds[led]=true;
        assert(keyboard_key_for_sensor(1,i)==i+1u);
        const keyboard_action_t *a=keyboard_action(1,i+1u,0);
        assert(a && a->key==i+1u);
        if(i==M1_FN_SENSOR) assert(a->type==0x11);
        else {
            assert(a->type==2);
            unsigned usage=m1_keys[i].usage;
            if(usage>=0xe0) assert(a->arg0==(1u<<(usage-0xe0)) && !a->arg1);
            else assert(!a->arg0 && a->arg1==usage);
        }
    }
    assert(!keyboard_action(1,0,0) && !keyboard_action(1,83,0));
    assert(m1_factory_cell(82)==UINT32_MAX && m1_led_index(82)==255);
    assert(m1_keys[73].usage==0xe3 && m1_keys[74].usage==0xe2 && m1_keys[76].usage==0xe6);
    assert(m1_keys[79].usage==0x50 && m1_keys[81].usage==0x4f);
    assert(keyboard_action(1,M1_TAB_SENSOR+1u,1)->arg0==0x70);
    assert(keyboard_editor_digit(1,16)==1 && keyboard_editor_digit(1,25)==10);
    assert(keyboard_editor_step(1,71)==1 && keyboard_editor_step(1,81)==-1);
    uint8_t frame[M1_LED_BYTES]={0};
    for(unsigned i=0;i<M1_KEY_COUNT;++i) {
        uint8_t r,g,b;
        keyboard_light_set(1,i,frame,i,i+1,i+2);
        keyboard_light_get(1,i,frame,&r,&g,&b);
        assert(r==i && g==i+1 && b==i+2);
    }
    assert(frame[0]==0 && frame[81*3]==72 && frame[72*3]==81);
}
static void bottom_light_diagnostic(void)
{
    uint8_t frame[M1_LED_BYTES],r,g,b;
    static const struct { unsigned sensor; uint8_t r,g,b; } expected[]={
        {72,255,255,255},{73,255,0,0},{74,0,255,0},{75,0,0,255},
        {76,255,255,255},{77,255,0,0},{78,0,255,0},
        {79,0,0,255},{80,255,255,255},{81,255,0,0},{70,0,255,0},
    };
    memset(frame,0xa5,sizeof(frame));m1_light_test_bottom(frame);
    for(unsigned sensor=0;sensor<M1_KEY_COUNT;++sensor) {
        r=g=b=0xff;
        keyboard_light_get(M1_PROFILE,sensor,frame,&r,&g,&b);
        unsigned n=0;
        while(n<sizeof(expected)/sizeof(expected[0]) && expected[n].sensor!=sensor)++n;
        if(n==sizeof(expected)/sizeof(expected[0]))assert(!r && !g && !b);
        else assert(r==expected[n].r && g==expected[n].g && b==expected[n].b);
    }
    assert(m1_led_index(70)!=m1_led_index(80));
}
static void factory_word(m1_factory_record_t *r,unsigned cell,unsigned value)
{ r->values[cell*2u]=value;r->values[cell*2u+1u]=value>>8; }
static void factory_calibration(void)
{
    m1_factory_record_t hi,lo;
    memset(&hi,0xff,sizeof(hi));memset(&lo,0xff,sizeof(lo));
    const uint8_t trailer[]={1,0x55,0xaa};
    memcpy(hi.trailer,trailer,3);memcpy(lo.trailer,trailer,3);
    for(unsigned i=0;i<M1_KEY_COUNT;++i) {
        unsigned cell=m1_factory_cell(i);
        factory_word(&hi,cell,3900-cell);factory_word(&lo,cell,1000+cell);
    }
    struct { uint32_t before;m1_factory_bounds_t bounds;uint32_t after; } out={.before=0x12345678,.after=0x87654321};
    assert(m1_factory_decode(&hi,&lo,&out.bounds)==M1_FACTORY_OK);
    for(unsigned i=0;i<M1_KEY_COUNT;++i) {
        unsigned cell=m1_factory_cell(i);
        assert(out.bounds.upper[i]==3901-cell && out.bounds.lower[i]==1001+cell);
    }
    const m1_factory_bounds_t previous=out.bounds;
    assert(m1_factory_decode(NULL,&lo,&out.bounds)==M1_FACTORY_ARGUMENT);
    assert(m1_factory_decode(&hi,NULL,&out.bounds)==M1_FACTORY_ARGUMENT);
    assert(m1_factory_decode(&hi,&lo,NULL)==M1_FACTORY_ARGUMENT);
    for(unsigned side=0;side<2;++side) {
        m1_factory_record_t *r=side?&lo:&hi;
        for(unsigned at=0;at<3;++at)for(unsigned value=0;value<256;++value) {
            if(value==trailer[at])continue;
            r->trailer[at]=value;
            assert(m1_factory_decode(&hi,&lo,&out.bounds)==
                (at?M1_FACTORY_MARKER:M1_FACTORY_UNCALIBRATED));
            assert(!memcmp(&out.bounds,&previous,sizeof(previous)));
            r->trailer[at]=trailer[at];
        }
    }
    for(unsigned i=0;i<M1_KEY_COUNT;++i) {
        unsigned cell=m1_factory_cell(i);
        const unsigned invalid_hi[]={0,M1_FACTORY_RELEASE_MIN_RAW-1u,M1_FACTORY_RELEASE_MAX_RAW+1u,65535};
        for(unsigned n=0;n<sizeof(invalid_hi)/sizeof(*invalid_hi);++n) {
            factory_word(&hi,cell,invalid_hi[n]);
            assert(m1_factory_decode(&hi,&lo,&out.bounds)==M1_FACTORY_RANGE);
            assert(!memcmp(&out.bounds,&previous,sizeof(previous)));
        }
        factory_word(&hi,cell,3900-cell);
        const unsigned invalid_lo[]={3900-cell,3901-cell,3900-cell-M1_CALIBRATION_MIN_SPAN_RAW+1u,4096,65535};
        for(unsigned n=0;n<sizeof(invalid_lo)/sizeof(*invalid_lo);++n) {
            factory_word(&lo,cell,invalid_lo[n]);
            assert(m1_factory_decode(&hi,&lo,&out.bounds)==M1_FACTORY_RANGE);
            assert(!memcmp(&out.bounds,&previous,sizeof(previous)));
        }
        factory_word(&lo,cell,1000+cell);
    }
    factory_word(&hi,0,M1_FACTORY_RELEASE_MIN_RAW);
    factory_word(&lo,0,M1_FACTORY_RELEASE_MIN_RAW-M1_CALIBRATION_MIN_SPAN_RAW);
    assert(m1_factory_decode(&hi,&lo,&out.bounds)==M1_FACTORY_OK);
    assert(out.bounds.upper[0]-out.bounds.lower[0]==M1_CALIBRATION_MIN_SPAN_RAW);
    factory_word(&hi,0,M1_FACTORY_RELEASE_MAX_RAW);factory_word(&lo,0,0);
    assert(m1_factory_decode(&hi,&lo,&out.bounds)==M1_FACTORY_OK);
    assert(out.bounds.lower[0]==1 && out.bounds.upper[0]==M1_FACTORY_RELEASE_MAX_RAW+1u);
    assert(out.before==0x12345678 && out.after==0x87654321);
    uint16_t released[M1_KEY_COUNT];
    for(unsigned i=0;i<M1_KEY_COUNT;++i)released[i]=2500+i;
    assert(m1_factory_bootstrap(released,&out.bounds));
    for(unsigned i=0;i<M1_KEY_COUNT;++i)
        assert(out.bounds.upper[i]==released[i] && out.bounds.lower[i]==released[i]-M1_STARTUP_TRAVEL_RAW);
    m1_factory_bounds_t bootstrap=out.bounds;
    released[81]=M1_FACTORY_RELEASE_MIN_RAW;
    assert(!m1_factory_bootstrap(released,&out.bounds) && !memcmp(&bootstrap,&out.bounds,sizeof(bootstrap)));
    released[81]=M1_FACTORY_RELEASE_MAX_RAW+2u;
    assert(!m1_factory_bootstrap(released,&out.bounds));
    assert(!m1_factory_bootstrap(NULL,&out.bounds) && !m1_factory_bootstrap(released,NULL));
}
static void lighting_encoding(void)
{
    uint8_t rgb[M1_LED_BYTES]={0},guard[M1_LED_WIRE_BYTES+2];
    memset(guard,0xa5,sizeof(guard));
    assert(!m1_lighting_encode(NULL,sizeof(rgb),guard+1,M1_LED_WIRE_BYTES));
    assert(!m1_lighting_encode(rgb,sizeof(rgb)-1,guard+1,M1_LED_WIRE_BYTES));
    assert(!m1_lighting_encode(rgb,sizeof(rgb),guard+1,M1_LED_WIRE_BYTES-1));
    for(unsigned i=0;i<sizeof(guard);++i)assert(guard[i]==0xa5);
    for(unsigned channel=0;channel<3;++channel)
        for(unsigned value=0;value<256;++value) {
            memset(rgb,0,sizeof(rgb));
            for(unsigned led=0;led<M1_KEY_COUNT;++led)rgb[led*3+channel]=value;
            assert(m1_lighting_encode(rgb,sizeof(rgb),guard+1,M1_LED_WIRE_BYTES));
            assert(guard[0]==0xa5 && guard[sizeof(guard)-1]==0xa5);
            for(unsigned led=0;led<M1_KEY_COUNT;++led)
                for(unsigned byte=0;byte<24;++byte) {
                    unsigned grb=channel==0?1:channel==1?0:2;
                    unsigned wanted=byte/8==grb && (value&(128u>>(byte%8)))?0xf0:0xc0;
                    assert(guard[1+led*24+byte]==wanted);
                }
        }
}
static void frame_set(m1_scan_t *scan,unsigned offset)
{
    uint16_t row[15];
    for(unsigned bank=0;bank<6;++bank) {
        for(unsigned rank=0;rank<15;++rank) row[rank]=offset+bank*15+rank;
        assert(m1_scan_bank(scan,bank,row));
    }
}
static void acquisition(void)
{
    m1_scan_t scan;
    uint16_t frame[82],row[15]={0}; uint32_t sequence=0;
    m1_scan_init(&scan);
    uint16_t adc=0; uint32_t battery_sequence=0;
    assert(!m1_scan_battery(&scan,&adc,&battery_sequence));
    assert(!m1_scan_take(&scan,frame,&sequence));
    frame_set(&scan,1000);
    assert(m1_scan_battery(&scan,&adc,&battery_sequence) && adc==1079 && battery_sequence==1);
    assert(m1_scan_take(&scan,frame,&sequence) && sequence==1);
    for(unsigned i=0;i<82;++i)
        assert(frame[i]==1001u+m1_keys[i].bank*15u+m1_keys[i].rank);
    assert(!m1_scan_take(&scan,frame,&sequence));
    frame_set(&scan,2000); frame_set(&scan,3000);
    assert(scan.pending==2 && m1_scan_take(&scan,frame,&sequence) && sequence==2);
    assert(frame[81]==2090);
    assert(m1_scan_take(&scan,frame,&sequence) && sequence==3);
    assert(frame[81]==3090);
    assert(m1_scan_bank(&scan,0,row));
    assert(!m1_scan_take(&scan,frame,&sequence));
    assert(!m1_scan_bank(&scan,2,row) && scan.errors==1 && !scan.next_bank);
    assert(!m1_scan_battery(&scan,&adc,&battery_sequence));
    row[2]=4096;
    assert(!m1_scan_bank(&scan,0,row) && scan.errors==2);
    frame_set(&scan,0); assert(scan.pending);
    assert(!m1_scan_bank(&scan,6,row) && !scan.pending);
    assert(!m1_scan_bank(&scan,0,NULL));
    frame_set(&scan,0);
    assert(!m1_scan_take(&scan,NULL,&sequence) && scan.pending);
    assert(m1_scan_take(&scan,frame,&sequence) && frame[0]==1);
    m1_scan_init(&scan);scan.sequence=UINT32_MAX-2u;
    for(unsigned round=0;round<3;++round) {
        for(unsigned i=0;i<M1_SCAN_QUEUE_FRAMES;++i)frame_set(&scan,i);
        for(unsigned i=0;i<M1_SCAN_QUEUE_FRAMES;++i) {
            assert(m1_scan_take(&scan,frame,&sequence));
            assert(sequence==(uint32_t)(UINT32_MAX-1u+round*M1_SCAN_QUEUE_FRAMES+i));
            assert(frame[0]==i+1u);
        }
        assert(!scan.pending && !scan.errors);
    }
    for(unsigned i=0;i<M1_SCAN_QUEUE_FRAMES;++i)frame_set(&scan,1000);
    memset(row,0,sizeof(row));
    for(unsigned i=0;i<M1_BANK_COUNT-1u;++i)assert(m1_scan_bank(&scan,i,row));
    assert(m1_scan_bank(&scan,M1_BANK_COUNT-1u,row));
    assert(scan.errors==1 && scan.pending==M1_SCAN_QUEUE_FRAMES &&
           scan.peak_pending==M1_SCAN_QUEUE_FRAMES && scan.battery_valid);
    assert(m1_scan_take(&scan,frame,&sequence));
    assert(sequence==(uint32_t)(UINT32_MAX-1u+3u*M1_SCAN_QUEUE_FRAMES+1u));
    assert(frame[0]==1001u);
    for(unsigned i=1;i<M1_SCAN_QUEUE_FRAMES;++i)
        assert(m1_scan_take(&scan,frame,&sequence));
    assert(frame[0]==1u && !scan.pending && scan.peak_pending==M1_SCAN_QUEUE_FRAMES);
}
static void battery(void)
{
    uint8_t previous=0;
    for(unsigned adc=0;adc<=M1_ADC_MAX;++adc) {
        uint8_t p=m1_battery_percent(adc);
        assert(p>=previous && p>=1 && p<=100); previous=p;
        /* Independent instruction-derived oracle, including the original
         * unsigned multiply/shift used instead of dividing by 135. */
        unsigned wanted=adc<=1145?1:adc>1704?100:adc>1280?
            20+(adc-1280)*80/425:
            20-((UINT64_C(0xf2b9d649)*(1280-adc)*20)>>39);
        assert(p==wanted);
    }
    m1_battery_t s;
    m1_battery_init(&s);
    uint8_t rgb[M1_LED_BYTES];
    memset(rgb,0xa5,sizeof(rgb)); m1_battery_lights(&s,false,rgb,0);
    for(unsigned i=0;i<sizeof(rgb);++i)assert(rgb[i]==0xa5);
    m1_battery_lights(&s,true,rgb,0);
    assert(rgb[m1_led_index(75)*3]); /* unknown is amber, not fabricated percent */
    uint32_t t=UINT32_MAX-100u;
    for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i) {
        assert(m1_battery_sample(&s,1280,true,false,t)==(i+1u==M1_BATTERY_FILTER_SAMPLES));
        assert(!m1_battery_sample(&s,0,true,false,t)); /* duplicate cannot pollute batch */
        t+=M1_BATTERY_SAMPLE_MS;
    }
    assert(s.valid && s.percent==20 && m1_battery_low(&s) && !m1_battery_critical(&s));
    m1_battery_lights(&s,true,rgb,M1_BATTERY_BLINK_MS);
    assert(rgb[m1_led_index(15)*3] && rgb[m1_led_index(16)*3] && !rgb[m1_led_index(17)*3]);
    assert(rgb[m1_led_index(74)*3]);
    /* Battery discharge display is monotonic; high spikes cannot increase it. */
    for(unsigned i=0;i<200;++i,t+=M1_BATTERY_SAMPLE_MS)m1_battery_sample(&s,1705,true,false,t);
    assert(s.percent==20);
    for(unsigned i=0;i<400;++i,t+=M1_BATTERY_SAMPLE_MS)m1_battery_sample(&s,1000,true,false,t);
    assert(s.percent==1 && m1_battery_critical(&s));
    /* Cable transition immediately invalidates the old charge state/batch. */
    m1_battery_sample(&s,1705,false,true,t); t+=M1_BATTERY_SAMPLE_MS;
    assert(!s.valid && !m1_battery_low(&s) && !m1_battery_critical(&s));
    for(unsigned i=0;i<20;++i,t+=M1_BATTERY_SAMPLE_MS)m1_battery_sample(&s,1705,false,true,t);
    assert(s.percent==99 && s.charger==M1_CHARGER_PIN_HIGH);
    assert(!m1_battery_sample(&s,4096,false,true,t) && !s.valid && s.charger==M1_CHARGER_UNKNOWN);
    for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i,t+=M1_BATTERY_SAMPLE_MS)
        m1_battery_sample(&s,1705,false,false,t);
    assert(s.percent==100);
    for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i,t+=M1_BATTERY_SAMPLE_MS)
        m1_battery_sample(&s,1705,false,true,t);
    assert(s.percent==99); /* charger qualification overrides monotonic display */
    /* Qualification is directional, not ten identical percentages. A moving
     * estimate must not indefinitely postpone discharge/charge indication. */
    for(unsigned charging=0;charging<2;++charging) {
        m1_battery_init(&s);
        for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i,t+=M1_BATTERY_SAMPLE_MS)
            m1_battery_sample(&s,charging?1100:1705,!charging,false,t);
        unsigned initial=s.percent;
        assert(initial==(charging?1u:100u));
        for(unsigned batch=0;batch<M1_BATTERY_CONFIRM_BATCHES;++batch) {
            unsigned adc=charging?1350+batch*(300u/M1_BATTERY_CONFIRM_BATCHES):
                                  1600-batch*(300u/M1_BATTERY_CONFIRM_BATCHES);
            for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i,t+=M1_BATTERY_SAMPLE_MS)
                m1_battery_sample(&s,adc,!charging,false,t);
            if(batch+1u<M1_BATTERY_CONFIRM_BATCHES) {
                assert(s.percent==initial && s.confirmations==batch+1u);
            } else {
                assert(s.percent==m1_battery_percent(s.average) && s.percent!=initial);
                assert(!s.confirmations);
            }
        }
    }
    m1_battery_init(&s);
    for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i,t+=M1_BATTERY_SAMPLE_MS)
        m1_battery_sample(&s,1280,true,false,t);
    for(unsigned batch=0;batch<M1_BATTERY_CONFIRM_BATCHES;++batch)
        for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i,t+=M1_BATTERY_SAMPLE_MS)
            m1_battery_sample(&s,1250-batch*(220u/M1_BATTERY_CONFIRM_BATCHES),true,false,t);
    assert(m1_battery_critical(&s)); /* falling estimates reach protection */
    /* Returning to the displayed level breaks directional qualification. */
    for(unsigned charging=0;charging<2;++charging) {
        m1_battery_init(&s);
        for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i,t+=M1_BATTERY_SAMPLE_MS)
            m1_battery_sample(&s,1280,!charging,false,t);
        for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i,t+=M1_BATTERY_SAMPLE_MS)
            m1_battery_sample(&s,charging?1400:1200,!charging,false,t);
        assert(s.percent==20 && s.confirmations==1);
        unsigned adc=2u*1280-s.average; /* next filtered average returns to 1280 */
        for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i,t+=M1_BATTERY_SAMPLE_MS)
            m1_battery_sample(&s,adc,!charging,false,t);
        assert(s.average==1280 && s.percent==20 && !s.confirmations);
        for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i,t+=M1_BATTERY_SAMPLE_MS)
            m1_battery_sample(&s,charging?1400:1200,!charging,false,t);
        assert(s.percent==20 && s.confirmations==1);
    }
}
static keyboard_raw_t raw;
static keyboard_midi_t midi;
static keyboard_menu_t menu;
static keyboard_calibration_t cal;
static keyboard_app_t app;
static uint16_t samples[82],lo[82],hi[82];
static uint32_t now;
static void frame(void) { keyboard_app_frame(&app,samples,82,1,lo,hi,true,now++); }
static void application(void)
{
    static const keyboard_app_ops_t ops={0};
    keyboard_app_init(&app,&raw,&midi,&menu,&cal,&ops);
    for(unsigned i=0;i<82;++i) { samples[i]=3900; lo[i]=1; hi[i]=4096; }
    frame(); assert(raw.armed);
    /* Exercise every real key, including all >65 IDs, through the shared engine. */
    for(unsigned i=0;i<82;++i) {
        if(i==M1_FN_SENSOR) continue;
        samples[i]=3499; frame();
        unsigned usage=m1_keys[i].usage;
        if(usage>=0xe0) assert(raw.engine.report.modifiers==(1u<<(usage-0xe0)));
        else assert(keyboard_report_get_usage(&raw.engine.report,usage));
        samples[i]=3900; frame();
        assert(!raw.engine.report.modifiers);
        if(usage<0xe0) assert(!keyboard_report_get_usage(&raw.engine.report,usage));
    }
    for(unsigned i=0;i<82;++i) if(i!=M1_FN_SENSOR) samples[i]=3000;
    frame();
    for(unsigned i=0;i<82;++i) {
        unsigned usage=m1_keys[i].usage;
        if(usage && usage<0xe0) assert(keyboard_report_get_usage(&raw.engine.report,usage));
    }
    for(unsigned i=0;i<82;++i) samples[i]=3900;
    frame();
    uint8_t rgb[M1_LED_BYTES], expected[M1_LED_BYTES],r,g,b,er,eg,eb;
    keyboard_app_lights(&app,lo,hi,rgb,now);
    keyboard_light_get(M1_PROFILE,56,rgb,&r,&g,&b); /* Enter follows White */
    assert(r && r==g && g==b);
    menu.effect=KEYBOARD_LIGHT_RAINBOW;
    keyboard_app_lights(&app,lo,hi,rgb,0);
    lighting_travel_frame(M1_PROFILE,raw.raw,app.input_lower,app.input_upper,true,expected);
    lighting_rainbow_frame(M1_PROFILE,M1_KEY_COUNT,expected,0);
    keyboard_light_get(M1_PROFILE,56,rgb,&r,&g,&b);
    keyboard_light_get(M1_PROFILE,56,expected,&er,&eg,&eb);
    assert(r==er && g==eg && b==eb); /* no keyboard-mode Enter override */
    /* Fn+Enter selects MIDI on release, not press. */
    samples[M1_FN_SENSOR]=samples[56]=3000; frame(); assert(!midi.mode);
    samples[M1_FN_SENSOR]=samples[56]=3900; frame(); frame(); assert(midi.mode==1);
    assert(midi.mapping[45]!=255); /* A: note mapped by shared default HID map. */
    assert(midi.mapping[81]==255); /* Dedicated Right arrow isn't silently a note. */
    keyboard_app_lights(&app,lo,hi,rgb,now);
#if !MIDI_COLOR_EFFECTS_ENABLED
    keyboard_light_get(M1_PROFILE,45,rgb,&r,&g,&b);
    assert(r && r==g && g==b); /* Rainbow is disabled for MIDI notes. */
#endif
    keyboard_light_get(M1_PROFILE,56,rgb,&r,&g,&b);
    assert(!r && !g && b==255); /* MIDI Enter remains the blue mode hint. */
}
static keyboard_save_result_t calibrated(const keyboard_calibration_t *candidate)
{
    assert(calibration_bounds_valid(M1_PROFILE,M1_KEY_COUNT,candidate->lower,candidate->upper));
    for(unsigned i=0;i<M1_KEY_COUNT;++i) {
        assert(candidate->upper[i]==2600+i && candidate->lower[i]==2300+i);
    }
    return KEYBOARD_SAVE_COMPLETE;
}
static void travel_domain(void)
{
    static const keyboard_app_ops_t ops={.save_calibration=calibrated};
    keyboard_app_init(&app,&raw,&midi,&menu,&cal,&ops);
    for(unsigned i=0;i<M1_KEY_COUNT;++i) {
        samples[i]=hi[i]=2600+i;lo[i]=hi[i]-M1_STARTUP_TRAVEL_RAW;
    }
    frame();assert(raw.armed);
    for(unsigned i=0;i<M1_KEY_COUNT;++i)assert(raw.raw[i]==4096);
    samples[45]=hi[45]-200;frame();
    assert(raw.raw[45]==2926 && keyboard_report_get_usage(&raw.engine.report,4));
    assert(lo[45]==1945 && hi[45]==2645); /* never persist control-domain bounds */
    samples[45]=hi[45];frame();assert(!keyboard_report_get_usage(&raw.engine.report,4));
    assert(keyboard_app_calibrate(&app,now,true));
    frame();now+=CALIBRATION_SETTLE_MS;frame();assert(cal.state==CAL_COLLECT);
    /* TMR electrical travel need not reach half of its released ADC reading.
     * Every key can be held/calibrated independently and in parallel. */
    for(unsigned i=0;i<M1_KEY_COUNT;++i)samples[i]=2300+i;
    frame();now+=CALIBRATION_HOLD_MS;frame();assert(cal.state==CAL_DONE);
    for(unsigned i=0;i<M1_KEY_COUNT;++i) {
        assert(lo[i]==2300+i && hi[i]==2600+i);samples[i]=hi[i];
    }
    frame();assert(raw.armed && raw.raw[45]==4096);
    samples[45]=lo[45];frame();assert(raw.raw[45]==1);
    hi[45]=lo[45];frame();assert(!raw.valid && !raw.armed);
}
static unsigned selected_count;
static m1_transport_t selected;
static bool drained,accept_switch;
static bool transport_drained(void *context) { assert(!context); return drained; }
static bool transport_select(void *context,m1_transport_t target)
{ assert(!context); ++selected_count; selected=target; return accept_switch; }
static bool accept_report(const keyboard_report_t *report) { (void)report; return true; }
static bool accept_midi(uint8_t a,uint8_t b,uint8_t c,uint8_t d)
{ (void)a;(void)b;(void)c;(void)d; return true; }
static unsigned paired_count;
static bool transport_pair(void *context,m1_transport_t target)
{ assert(!context);++paired_count;selected=target;return accept_switch; }
static void pairing_controls(void)
{
    static const keyboard_app_ops_t ops={0};
    const m1_transport_ops_t transport={.drained=transport_drained,
        .select=transport_select,.pair=transport_pair};
    const m1_transport_t targets[]={0,1,2,5};
    for(unsigned f=1;f<=4;++f) {
        m1_controls_t s;
        keyboard_app_init(&app,&raw,&midi,&menu,&cal,&ops);
        assert(m1_controls_bind(&s,&app,targets[f-1],&transport,NULL));
        for(unsigned i=0;i<82;++i) { samples[i]=3900;lo[i]=1;hi[i]=4096; }
        now=UINT32_MAX-100;frame();frame();
        samples[M1_FN_SENSOR]=samples[f]=3000;frame();
        assert(s.pending==f && !s.pairing);
        now=s.held_at+M1_PAIR_HOLD_MS-1;frame();assert(!s.pairing);
        frame();assert(s.pairing && !s.switching); /* unsigned time wrap */
        unsigned calls=paired_count;
        m1_controls_service(&s,&app,now);assert(paired_count==calls);
        samples[f]=3900;frame();assert(s.switching && s.pairing);
        drained=false;accept_switch=true;
        keyboard_app_service(&app,now,true,accept_report,accept_midi);
        m1_controls_service(&s,&app,now);assert(paired_count==calls);
        drained=true;m1_controls_service(&s,&app,now);
        assert(paired_count==calls+1 && !s.switching && !s.pairing);
        assert(s.current==targets[f-1] && s.neutral_required && !s.errors);
        samples[M1_FN_SENSOR]=3900;frame();frame();
        /* A short same-slot press is not a pairing request. */
        samples[M1_FN_SENSOR]=samples[f]=3000;frame();
        samples[f]=3900;frame();assert(!s.switching && !s.pairing);
        assert(paired_count==calls+1);
    }
    for(unsigned index=0;index<2;++index) {
        unsigned sensor=index?M1_SPACE_SENSOR:5;
        m1_controls_t s;
        keyboard_app_init(&app,&raw,&midi,&menu,&cal,&ops);
        assert(m1_controls_bind(&s,&app,M1_TRANSPORT_USB,&transport,NULL));
        for(unsigned i=0;i<82;++i)samples[i]=3900;
        frame();frame();samples[M1_FN_SENSOR]=samples[sensor]=3000;frame();
        now=s.held_at+M1_PAIR_HOLD_MS;frame();assert(!s.pairing);
        samples[M1_FN_SENSOR]=samples[sensor]=3900;frame();assert(!s.switching);
    }
}
static void controls(void)
{
    static const keyboard_app_ops_t ops={0};
    const m1_transport_ops_t transport={.drained=transport_drained,.select=transport_select};
    m1_controls_t s;
    m1_battery_t battery;
    m1_battery_init(&battery);
    keyboard_app_init(&app,&raw,&midi,&menu,&cal,&ops);
    assert(!m1_controls_bind(&s,&app,3,&transport,&battery));
    assert(m1_controls_bind(&s,&app,M1_TRANSPORT_USB,&transport,&battery));
    for(unsigned i=0;i<82;++i) { samples[i]=3900; lo[i]=1; hi[i]=4096; }
    frame();
    for(unsigned f=1;f<=5;++f) {
        samples[f]=3000; frame();
        assert(keyboard_report_get_usage(&raw.engine.report,0x39+f));
        samples[f]=3900; frame();
    }
    /* Leave MIDI only after a preview is released; old host must see cleanup. */
    keyboard_midi_toggle(&midi,&raw,now); frame(); assert(midi.mode);
    samples[M1_FN_SENSOR]=samples[1]=3000; frame();
    assert(s.pending==1 && midi.mode && !s.switching);
    for(unsigned repeat=0;repeat<32;++repeat) {
        frame();assert(s.pending==1 && !raw.armed && app.frame_valid);
        assert(app.system_observing(&app,app.system_context));
        for(unsigned key=0;key<M1_KEY_COUNT;++key)
            assert(!raw.velocity[key].ready && !raw.velocity[key].pending);
    }
    samples[1]=3900; frame();
    assert(s.switching && !midi.mode && s.current==M1_TRANSPORT_USB);
    for(unsigned i=0;i<MIDI_CLEANUP_EVENTS+5;++i) {
        keyboard_app_service(&app,now,true,accept_report,accept_midi);
        m1_controls_service(&s,&app,now);
    }
    assert(!selected_count && s.switching); /* queued is not delivered */
    drained=true; accept_switch=false;
    m1_controls_service(&s,&app,now);
    assert(selected_count==1 && s.current==M1_TRANSPORT_USB);
    accept_switch=true; m1_controls_service(&s,&app,now);
    assert(selected==M1_TRANSPORT_BT1 && s.current==selected && !s.switching);
    samples[M1_FN_SENSOR]=3900; frame(); frame();
    /* Wireless Fn+Enter cannot enter MIDI or advertise it. */
    samples[M1_FN_SENSOR]=samples[56]=3000; frame(); assert(!midi.mode && !menu.pending);
    samples[M1_FN_SENSOR]=samples[56]=3900; frame(); frame();
    /* A saved or externally changed MIDI mode is also forced off. */
    midi.mode=1; frame(); assert(!midi.mode);
    frame();
    samples[M1_FN_SENSOR]=samples[75]=3000; frame();
    assert(s.battery_show && !raw.armed);
    assert(app.system_observing(&app,app.system_context));
    uint8_t rgb[M1_LED_BYTES]; keyboard_app_lights(&app,lo,hi,rgb,now);
    assert(rgb[m1_led_index(75)*3]);
    samples[75]=3900; frame(); assert(!s.battery_show && s.neutral_required);
    samples[M1_FN_SENSOR]=3900; frame(); frame();
    assert(!app.system_observing(&app,app.system_context) && raw.armed);
    /* Mode table includes exactly three BT slots, RF and USB. */
    const m1_transport_t expected[]={0,1,2,5,6};
    for(unsigned f=2;f<=5;++f) {
        samples[M1_FN_SENSOR]=samples[f]=3000; frame();
        samples[M1_FN_SENSOR]=samples[f]=3900; frame();
        keyboard_app_service(&app,now,true,accept_report,accept_midi);
        m1_controls_service(&s,&app,now);
        assert(s.current==expected[f-1]); frame(); frame();
    }
    assert(!menu.midi_blocked);
    samples[M1_FN_SENSOR]=samples[56]=3000; frame();
    samples[M1_FN_SENSOR]=samples[56]=3900; frame(); frame(); assert(midi.mode);
    /* A failed adapter times out without changing the selected transport. */
    samples[M1_FN_SENSOR]=samples[4]=3000; frame();
    samples[M1_FN_SENSOR]=samples[4]=3900; frame(); assert(s.switching);
    now+=M1_TRANSPORT_SWITCH_TIMEOUT_MS; frame();
    m1_controls_service(&s,&app,now);
    assert(!s.switching && s.errors==1 && s.current==M1_TRANSPORT_USB);
    for(unsigned i=0;i<82;++i)samples[i]=3900;
    frame(); frame();
    samples[M1_FN_SENSOR]=samples[75]=3000; frame(); assert(s.battery_show);
    keyboard_app_service(&app,now+SCAN_STALE_MS,true,accept_report,accept_midi);
    m1_controls_service(&s,&app,now+SCAN_STALE_MS);
    assert(!s.pending && !s.battery_show && s.neutral_required);
    samples[M1_FN_SENSOR]=samples[75]=3900; frame(); frame();
    samples[M1_FN_SENSOR]=samples[56]=3000; frame(); assert(menu.pending==MENU_MODE);
    assert(m1_controls_bind(&s,&app,M1_TRANSPORT_BT2,&transport,&battery));
    samples[M1_FN_SENSOR]=samples[56]=3900; frame(); frame();
    assert(!midi.mode && !menu.pending);
}
static void power_policy(void)
{
    m1_power_t power;
    m1_battery_t battery={.valid=true,.percent=80};
    m1_power_input_t in={.transport=M1_TRANSPORT_BT1,.selector=1,.battery=&battery};
    m1_power_init(&power,2,3);
    for(unsigned i=0;i<2*M1_POWER_QUALIFY_TICKS-1;++i)m1_power_tick(&power,&in);
    assert(!power.sleep_requested);
    m1_power_tick(&power,&in);
    assert(power.sleep_requested && power.radio_command==5);
    assert(!m1_power_can_sleep(&power,true,true,true,true,true));
    assert(!m1_power_radio_committed(&power,3));
    assert(m1_power_radio_committed(&power,5));
    for(unsigned mask=0;mask<32;++mask)
        assert(m1_power_can_sleep(&power,mask&1,mask&2,mask&4,mask&8,mask&16)==(mask==31));
    battery.percent=5;m1_power_tick(&power,&in);
    assert(power.sleep_requested && power.critical_latched && power.radio_command==3 && !power.radio_committed);
    assert(!m1_power_radio_committed(&power,5) && m1_power_radio_committed(&power,3));
    in.externally_powered=true;m1_power_tick(&power,&in);
    assert(!power.critical_latched);in.externally_powered=false;battery.percent=80;
    m1_power_woke(&power); assert(!power.sleep_requested && !power.radio_committed);
    in.transport=M1_TRANSPORT_RADIO;
    for(unsigned i=0;i<3*M1_POWER_QUALIFY_TICKS;++i)m1_power_tick(&power,&in);
    assert(power.sleep_requested && power.radio_command==3);
    in.externally_powered=true; m1_power_tick(&power,&in);
    assert(!power.sleep_requested);
    in.externally_powered=false; in.activity=true;
    for(unsigned i=0;i<1000;++i)m1_power_tick(&power,&in);
    assert(!power.elapsed && !power.sleep_requested);
    in.activity=false; in.fast_idle=true; in.selector=3;
    for(unsigned i=0;i<M1_POWER_UNSELECTED_STEPS*M1_POWER_FAST_QUALIFY_TICKS;++i)
        m1_power_tick(&power,&in);
    assert(power.sleep_requested && power.radio_command==3);
    m1_power_woke(&power); in.selector=1;
    m1_power_init(&power,0,0); /* connected idle disabled, not critical safety */
    for(unsigned i=0;i<1000;++i)m1_power_tick(&power,&in);
    assert(!power.sleep_requested);
    battery.percent=5; in.activity=true;
    for(unsigned i=0;i<M1_POWER_CRITICAL_STEPS*M1_POWER_FAST_QUALIFY_TICKS;++i)
        m1_power_tick(&power,&in);
    assert(power.critical_latched && power.sleep_requested && power.radio_command==3);
    in.externally_powered=true; m1_power_tick(&power,&in);
    assert(!power.critical_latched && !power.sleep_requested);
    /* An enumerated USB host must never be interrupted by sleep; the transport
     * number alone is not the gate, because a selected-but-unenumerated USB
     * link is exactly the battery-only case that has to be allowed to sleep. */
    in.externally_powered=false; in.transport=M1_TRANSPORT_USB; in.host_link=true;
    for(unsigned i=0;i<1000;++i)m1_power_tick(&power,&in);
    assert(!power.critical_latched && !power.sleep_requested);
    in.host_link=false; in.selector=1; in.fast_idle=true;
    in.activity=false; battery.percent=80;
    m1_power_init(&power,2,3);
    for(unsigned i=0;i<3*M1_POWER_FAST_QUALIFY_TICKS && !power.sleep_requested;++i)
        m1_power_tick(&power,&in);
    assert(power.sleep_requested && power.radio_command==5);
}
static void radio_packets(void)
{
    uint8_t payload[M1_RADIO_PAYLOAD_MAX];
    for(unsigned i=0;i<sizeof(payload);++i)payload[i]=(uint8_t)(i*37u+0x10u);
    m1_radio_packet_t packet,unchanged;
    m1_radio_reply_t reply,saved;
    memset(&unchanged,0xa5,sizeof(unchanged));
    memset(&saved,0x5a,sizeof(saved));
    for(unsigned mode=0;mode<=7;++mode) {
        packet=unchanged;
        bool valid=mode<=2 || mode==5;
        assert(m1_radio_make_pair(&packet,mode)==valid);
        if(!valid) { assert(!memcmp(&packet,&unchanged,sizeof(packet)));continue; }
        assert(packet.bytes[0]==M1_RADIO_CONTROL);
        if(mode==5)assert(packet.size==8 && !memcmp(packet.bytes+1,"\2\0\1\1",4));
        else {
            assert(packet.size==36 && packet.bytes[1]==33);
            assert(packet.bytes[2]==2 && packet.bytes[3]==12);
            assert(!memcmp(packet.bytes+4,"MIDI-Typist",11));
            assert(packet.bytes[15]=='1'+mode);
            for(unsigned i=16;i<35;++i)assert(!packet.bytes[i]);
        }
        assert(m1_radio_decode(packet.bytes,packet.size,&reply));
        for(unsigned i=packet.size;i<sizeof(packet.bytes);++i)assert(!packet.bytes[i]);
    }
    assert(!m1_radio_make_pair(NULL,0) && !m1_radio_make_pair(NULL,5));
    const uint8_t opcodes[]={M1_RADIO_REPORT,M1_RADIO_BATTERY,
        M1_RADIO_STATUS_REQUEST,M1_RADIO_MODE,M1_RADIO_CONTROL};
    for(unsigned op=0;op<sizeof(opcodes);++op) {
        for(unsigned count=1;count<=sizeof(payload);++count) {
            packet=unchanged;
            assert(m1_radio_encode(&packet,opcodes[op],payload,count));
            assert(packet.size==((count+6u)&~3u));
            assert(packet.bytes[0]==opcodes[op] && packet.bytes[1]==count);
            assert(!memcmp(packet.bytes+2,payload,count));
            uint8_t sum=0;
            for(unsigned i=0;i<count;++i)sum+=payload[i];
            assert(packet.bytes[count+2u]==sum);
            for(unsigned i=count+3u;i<sizeof(packet.bytes);++i)assert(!packet.bytes[i]);
            assert(m1_radio_decode(packet.bytes,packet.size,&reply));
            assert(reply.kind==payload[0] && reply.length==count-1u);
            assert(!memcmp(reply.data,payload+1,count-1u));
            for(unsigned i=count-1u;i<sizeof(reply.data);++i)assert(!reply.data[i]);
            /* No read beyond the supplied transfer, even with a valid checksum. */
            for(unsigned n=0;n<count+3u;++n) {
                reply=saved;
                assert(!m1_radio_decode(packet.bytes,n,&reply));
                assert(!memcmp(&reply,&saved,sizeof(reply)));
            }
            packet.bytes[count+2u]^=1;
            reply=saved;
            assert(!m1_radio_decode(packet.bytes,packet.size,&reply));
            assert(!memcmp(&reply,&saved,sizeof(reply)));
        }
    }
    for(unsigned opcode=0;opcode<256;++opcode) {
        bool allowed=false;
        for(unsigned i=0;i<sizeof(opcodes);++i)allowed|=opcode==opcodes[i];
        packet=unchanged;
        assert(m1_radio_encode(&packet,opcode,payload,1)==allowed);
        if(!allowed)assert(!memcmp(&packet,&unchanged,sizeof(packet)));
    }
    packet=unchanged;
    assert(!m1_radio_encode(&packet,M1_RADIO_MODE,payload,0));
    assert(!m1_radio_encode(&packet,M1_RADIO_MODE,payload,sizeof(payload)+1));
    assert(!m1_radio_encode(&packet,M1_RADIO_MODE,NULL,1));
    assert(!m1_radio_encode(NULL,M1_RADIO_MODE,payload,1));
    assert(!memcmp(&packet,&unchanged,sizeof(packet)));
    assert(m1_radio_encode(&packet,M1_RADIO_MODE,packet.bytes,1));
    assert(packet.bytes[2]==0xa5 && packet.bytes[3]==0xa5);
    m1_radio_make_poll(&packet);
    assert(packet.size==68 && packet.bytes[0]==9);
    for(unsigned i=1;i<sizeof(packet.bytes);++i)assert(!packet.bytes[i]);
    m1_radio_make_poll(NULL);
    for(unsigned count=0;count<256;++count) {
        packet.bytes[1]=count;
        assert(m1_radio_decode(packet.bytes,80,&reply)==(count>=1 && count<=65));
    }
    assert(!m1_radio_decode(NULL,80,&reply));
    assert(!m1_radio_decode(packet.bytes,81,&reply));
    assert(!m1_radio_decode(packet.bytes,80,NULL));
    const uint8_t status[]={M1_RADIO_REPLY_STATUS,0x25,3,6};
    assert(m1_radio_encode(&packet,M1_RADIO_REPORT,status,sizeof(status)));
    assert(m1_radio_decode(packet.bytes,packet.size,&reply));
    m1_radio_status_t decoded;
    assert(m1_radio_status(&reply,&decoded));
    assert(decoded.flags==0x25 && decoded.state==3 && decoded.mode==6);
    assert(!m1_radio_status(NULL,&decoded) && !m1_radio_status(&reply,NULL));
    reply.length=2; assert(!m1_radio_status(&reply,&decoded));
    reply.length=65; assert(!m1_radio_status(&reply,&decoded));
    reply.length=3; reply.kind=0; assert(!m1_radio_status(&reply,&decoded));
    assert(decoded.flags==0x25 && decoded.state==3 && decoded.mode==6);
}
static void check_radio_keys(const m1_radio_keyboard_t *s,const keyboard_report_t *report)
{
    m1_radio_packet_t a,b;
    assert(m1_radio_keyboard_packet(s,1,&a) && m1_radio_keyboard_packet(s,2,&b));
    assert(a.size==12 && a.bytes[0]==0x81 && a.bytes[1]==8 && a.bytes[2]==1);
    assert(b.size==20 && b.bytes[0]==0x81 && b.bytes[1]==16 && b.bytes[2]==2);
    assert(a.bytes[3]==report->modifiers && !s->rollover);
    for(unsigned usage=4;usage<=KEYBOARD_NKRO_USAGE_MAX;++usage) {
        unsigned found=0;
        for(unsigned i=0;i<6;++i)found+=a.bytes[4+i]==usage;
        if(usage<120)found+=!!(b.bytes[3+usage/8]&(1u<<(usage%8)));
        assert(found==(unsigned)keyboard_report_get_usage(report,usage));
    }
    unsigned sum_a=0,sum_b=0;
    for(unsigned i=2;i<10;++i)sum_a+=a.bytes[i];
    for(unsigned i=2;i<18;++i)sum_b+=b.bytes[i];
    assert(a.bytes[10]==(uint8_t)sum_a && !a.bytes[11]);
    assert(b.bytes[18]==(uint8_t)sum_b && !b.bytes[19]);
}
static void radio_keyboard(void)
{
    m1_radio_keyboard_t state={0},saved;
    keyboard_report_t report={0};
    assert(!m1_radio_keyboard_update(NULL,&report));
    assert(!m1_radio_keyboard_update(&state,NULL));
    report.reserved=1;saved=state;
    assert(!m1_radio_keyboard_update(&state,&report) && !memcmp(&state,&saved,sizeof(state)));
    report.reserved=0;
    for(unsigned usage=4;usage<=KEYBOARD_NKRO_USAGE_MAX;++usage) {
        state=(m1_radio_keyboard_t){0};report=(keyboard_report_t){0};
        assert(keyboard_report_set_usage(&report,usage,true));report.modifiers=0xa5;
        assert(m1_radio_keyboard_update(&state,&report));check_radio_keys(&state,&report);
    }
    state=(m1_radio_keyboard_t){0};report=(keyboard_report_t){0};
    /* All normal-key usages at once, then independent releases: no bitmap
     * owner migrates into a slot when the older six keys release. */
    for(unsigned usage=4;usage<120;++usage)keyboard_report_set_usage(&report,usage,true);
    assert(m1_radio_keyboard_update(&state,&report));check_radio_keys(&state,&report);
    for(unsigned usage=4;usage<120;++usage) {
        keyboard_report_set_usage(&report,usage,false);
        assert(m1_radio_keyboard_update(&state,&report));check_radio_keys(&state,&report);
        if(usage==9)for(unsigned i=0;i<6;++i)assert(!state.slots[i]);
    }
    /* Extended simultaneous remaps fit six slots, but cannot corrupt the
     * 15-byte bitmap on a seventh extended usage. */
    for(unsigned usage=120;usage<126;++usage)keyboard_report_set_usage(&report,usage,true);
    assert(m1_radio_keyboard_update(&state,&report));check_radio_keys(&state,&report);
    keyboard_report_set_usage(&report,126,true);
    assert(m1_radio_keyboard_update(&state,&report) && state.rollover);
    m1_radio_packet_t packet;
    assert(m1_radio_keyboard_packet(&state,1,&packet));
    for(unsigned i=0;i<6;++i)assert(packet.bytes[4+i]==1);
    keyboard_report_set_usage(&report,120,false);
    assert(m1_radio_keyboard_update(&state,&report));check_radio_keys(&state,&report);
    assert(!m1_radio_keyboard_packet(&state,0,&packet));
    assert(!m1_radio_keyboard_packet(NULL,1,&packet));
    assert(!m1_radio_keyboard_packet(&state,1,NULL));
    /* Deterministic evolving polyphony, modifiers and unordered releases. */
    state=(m1_radio_keyboard_t){0};report=(keyboard_report_t){0};uint32_t random=7;
    for(unsigned i=0;i<2000;++i) {
        random=random*1664525u+1013904223u;unsigned usage=4+(random>>8)%116u;
        keyboard_report_set_usage(&report,usage,!keyboard_report_get_usage(&report,usage));
        report.modifiers=random>>24;
        assert(m1_radio_keyboard_update(&state,&report));check_radio_keys(&state,&report);
    }
}
static void wake_baseline(m1_wake_t *s,uint16_t *frame,uint32_t *sequence)
{
    for(unsigned i=0;i<M1_WAKE_ACQUIRE_FRAMES;++i) {
        assert(m1_wake_frame(s,frame,++*sequence)==M1_WAKE_WAIT);
        unsigned acquired=s->acquired;
        assert(m1_wake_frame(s,NULL,*sequence)==M1_WAKE_WAIT); /* duplicate, not data */
        assert(s->acquired==acquired);
    }
    assert(s->acquired==M1_WAKE_ACQUIRE_FRAMES);
}
static void wake_policy(void)
{
    m1_wake_t s;
    uint16_t values[M1_KEY_COUNT],captured[M1_KEY_COUNT];
    bool enabled[M1_KEY_COUNT],triggered[M1_KEY_COUNT];
    uint32_t seq;
    /* Every real key, strict threshold and unchanged full-frame capture. */
    for(unsigned key=0;key<M1_KEY_COUNT;++key) {
        for(unsigned i=0;i<M1_KEY_COUNT;++i)values[i]=4000;
        m1_wake_init(&s,NULL);seq=UINT32_MAX-4;wake_baseline(&s,values,&seq);
        values[key]=4000-M1_WAKE_DROP_COUNTS;
        assert(m1_wake_frame(&s,values,++seq)==M1_WAKE_WAIT);
        --values[key];assert(m1_wake_frame(&s,values,++seq)==M1_WAKE_KEYS);
        assert(m1_wake_snapshot(&s,captured,triggered));
        assert(!memcmp(captured,values,sizeof(values)));
        for(unsigned i=0;i<M1_KEY_COUNT;++i)assert(triggered[i]==(i==key));
        values[key]=4000;
        assert(m1_wake_frame(&s,values,++seq)==M1_WAKE_KEYS);
        assert(m1_wake_snapshot(&s,captured,triggered));
        assert(captured[key]==4000-M1_WAKE_DROP_COUNTS-1);
    }
    for(unsigned i=0;i<M1_KEY_COUNT;++i) { values[i]=4096;enabled[i]=(i&1u)!=0; }
    m1_wake_init(&s,enabled);seq=0;wake_baseline(&s,values,&seq);
    for(unsigned i=0;i<M1_KEY_COUNT;++i)values[i]=1;
    assert(m1_wake_frame(&s,values,++seq)==M1_WAKE_KEYS);
    assert(m1_wake_snapshot(&s,captured,triggered));
    assert(!memcmp(triggered,enabled,sizeof(enabled)));
    m1_wake_init(&s,s.enabled);assert(!memcmp(s.enabled,enabled,sizeof(enabled)));
    /* The last acquisition frame replaces baseline and cannot trigger wake. */
    m1_wake_init(&s,NULL);seq=0;
    for(unsigned i=0;i<M1_WAKE_ACQUIRE_FRAMES;++i) {
        for(unsigned key=0;key<M1_KEY_COUNT;++key)values[key]=i==M1_WAKE_ACQUIRE_FRAMES-1?1000:4000;
        assert(m1_wake_frame(&s,values,++seq)==M1_WAKE_WAIT);
    }
    assert(s.baseline[0]==1000);
    /* Fifty unequal readings refresh BEFORE comparison; equality resets drift. */
    for(unsigned i=0;i<M1_KEY_COUNT;++i)values[i]=4000;
    m1_wake_init(&s,NULL);seq=0;wake_baseline(&s,values,&seq);
    values[0]=3999;
    for(unsigned i=1;i<M1_WAKE_REFRESH_FRAMES;++i)
        assert(m1_wake_frame(&s,values,++seq)==M1_WAKE_WAIT);
    values[0]=1000;assert(m1_wake_frame(&s,values,++seq)==M1_WAKE_WAIT);
    assert(s.baseline[0]==1000 && !s.drift[0]);
    values[0]=1001;assert(m1_wake_frame(&s,values,++seq)==M1_WAKE_WAIT);
    assert(s.drift[0]==1);
    values[0]=1000;assert(m1_wake_frame(&s,values,++seq)==M1_WAKE_WAIT && !s.drift[0]);
    /* Faults are latched, never publish partial/invalid frames or fake keys. */
    const unsigned invalid[]={0,4097,65535};
    for(unsigned bad=0;bad<sizeof(invalid)/sizeof(invalid[0]);++bad) {
        m1_wake_init(&s,NULL);seq=0;wake_baseline(&s,values,&seq);
        uint16_t prior=s.frame[81];values[81]=invalid[bad];
        assert(m1_wake_frame(&s,values,++seq)==M1_WAKE_FAULT);
        assert(s.frame[81]==prior && !m1_wake_snapshot(&s,captured,triggered));
        values[81]=4000;assert(m1_wake_frame(&s,values,++seq)==M1_WAKE_FAULT);
    }
    m1_wake_init(&s,NULL);seq=0;wake_baseline(&s,values,&seq);
    assert(m1_wake_frame(&s,values,seq+2)==M1_WAKE_FAULT);
    m1_wake_init(&s,NULL);assert(m1_wake_frame(&s,NULL,1)==M1_WAKE_FAULT);
    memset(captured,0xa5,sizeof(captured));
    assert(!m1_wake_snapshot(NULL,captured,triggered));
    assert(!m1_wake_snapshot(&s,captured,triggered));
    for(unsigned i=0;i<M1_KEY_COUNT;++i)assert(captured[i]==0xa5a5);
}
int main(void)
{
    mapping(); bottom_light_diagnostic(); factory_calibration(); acquisition(); application(); travel_domain(); lighting_encoding(); battery(); controls(); pairing_controls(); power_policy(); radio_packets(); radio_keyboard(); wake_policy();
    puts("M1: mapping, scan, lighting, application, battery, transport controls, radio codec and wake policy passed");
    return 0;
}
