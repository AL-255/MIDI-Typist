#include "keyboard_raw.h"
#include "keyboard_app.h"
#include "keyboard_midi.h"
#include "keyboard_menu.h"
#include "keyboard_calibration.h"
#include "keyboard_sample.h"
#include "keyboard_layout.h"
#include "keyboard_telemetry.h"
#include "synthetic_board.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static keyboard_raw_t raw;
static keyboard_midi_t midi;
static keyboard_menu_t menu;
static keyboard_calibration_t app_cal;
static keyboard_app_t app;
static uint16_t samples[SYN_COUNT],lo[SYN_COUNT],hi[SYN_COUNT];
static uint8_t rgb[LIGHTING_FRAME_SIZE],packets[1024][4];
static unsigned logged;
static uint32_t now;
static unsigned saved,loaded,resets;
static keyboard_save_result_t save_result;
static bool refuse_output;
static keyboard_report_t hid;
static bool load_bounds(uint8_t profile,uint8_t count,uint16_t *lower,uint16_t *upper)
{
    assert(((profile==SYN_PROFILE && count==SYN_COUNT) || (profile==43 && count==7)) && lower && upper);
    ++loaded; return false;
}
static keyboard_save_result_t save_bounds(const keyboard_calibration_t *cal)
{
    assert(cal->completed==SYN_COUNT && cal->lower[103]==1000);
    ++saved; return save_result;
}
static bool clear_settings(void) { ++resets; return true; }
static const keyboard_app_ops_t ops={load_bounds,save_bounds,clear_settings,NULL,NULL};
static bool send_hid(const keyboard_report_t *report)
{
    if(refuse_output) return false;
    hid=*report; return true;
}
static bool send(uint8_t a,uint8_t b,uint8_t c,uint8_t d)
{
    assert(logged<1024);
    memcpy(packets[logged++],(uint8_t[]){a,b,c,d},4); return true;
}
static void drain(void) { for(unsigned i=0;i<300;++i) keyboard_midi_service(&midi,now,send); }
static void frame(void)
{
    keyboard_app_frame(&app,samples,SYN_COUNT,SYN_PROFILE,lo,hi,true,now++);
}
static void init(void)
{
    synthetic_board_init();
    keyboard_app_init(&app,&raw,&midi,&menu,&app_cal,&ops);
    for(unsigned i=0;i<SYN_COUNT;++i) { samples[i]=3900; lo[i]=1000; hi[i]=4000; }
    saved=loaded=resets=0; refuse_output=false; save_result=KEYBOARD_SAVE_COMPLETE;
    logged=now=0; frame(); drain(); logged=0; assert(raw.armed && loaded==1);
}
static void chord(unsigned sensor)
{
    samples[SYN_FN]=samples[sensor]=3000; frame();
    samples[SYN_FN]=samples[sensor]=3900; frame(); frame(); drain(); logged=0;
}
static void normalizer(void)
{
    uint16_t value=123;
    assert(!keyboard_sample_normalize(10,20,20,&value) && value==123);
    assert(!keyboard_sample_normalize(10,0,65535,NULL));
    for(unsigned n=1;n<=4096;++n) {
        assert(keyboard_sample_normalize(n,4096,1,&value) && value==n);
        assert(keyboard_sample_normalize(n,1,4096,&value) && value==4097-n);
    }
    uint16_t prior=4096;
    for(unsigned n=0;n<=65535;++n) {
        assert(keyboard_sample_normalize(n,0,65535,&value));
        assert(value>=1 && value<=4096 && value<=prior); prior=value;
    }
    assert(value==1);
    assert(keyboard_sample_normalize(0,1000,3000,&value) && value==4096);
    assert(keyboard_sample_normalize(65535,1000,3000,&value) && value==1);
    uint16_t samples[4],lower[4]={1,1000,2000,3968},upper[4]={4096,1700,3000,4096},out[4];
    for(unsigned n=1;n<=4096;++n) {
        for(unsigned i=0;i<4;++i)samples[i]=n;
        assert(keyboard_samples_travel(samples,lower,upper,4,128,out));
        for(unsigned i=0;i<4;++i) {
            assert(keyboard_sample_normalize(n,upper[i],lower[i],&value));
            assert(out[i]==value);
        }
    }
    assert(!keyboard_samples_travel(NULL,lower,upper,4,128,out));
    assert(!keyboard_samples_travel(samples,lower,upper,0,128,out));
    assert(!keyboard_samples_travel(samples,lower,upper,4,0,out));
    assert(!keyboard_samples_travel(samples,lower,upper,4,4096,out));
    assert(!keyboard_samples_travel(samples,lower,upper,4,129,out));
    for(unsigned i=0;i<4;++i) {
        samples[i]=0;assert(!keyboard_samples_travel(samples,lower,upper,4,128,out));
        samples[i]=4097;assert(!keyboard_samples_travel(samples,lower,upper,4,128,out));
        samples[i]=4096;
        uint16_t saved=lower[i];lower[i]=0;
        assert(!keyboard_samples_travel(samples,lower,upper,4,128,out));lower[i]=saved;
        saved=upper[i];upper[i]=lower[i];
        assert(!keyboard_samples_travel(samples,lower,upper,4,128,out));
        upper[i]=4097;assert(!keyboard_samples_travel(samples,lower,upper,4,128,out));upper[i]=saved;
    }
}
static void performance(void)
{
    init(); samples[100]=3499; frame();
    assert(keyboard_report_get_usage(&raw.engine.report,4));
    samples[100]=3900; frame();
    assert(!keyboard_report_get_usage(&raw.engine.report,4));
    samples[SYN_RALT]=3000; frame();
    assert(raw.engine.report.modifiers==64 && !keyboard_report_get_usage(&raw.engine.report,0x50));
    samples[SYN_RALT]=3900; frame(); assert(!raw.engine.report.modifiers);
    samples[103]=3000; frame(); assert(keyboard_report_get_usage(&raw.engine.report,0x87));
    samples[103]=3900; frame(); assert(!keyboard_report_get_usage(&raw.engine.report,0x87));
    chord(SYN_ENTER); assert(midi.mode==1 && raw.armed);
    samples[SYN_SPACE]=3499; frame(); drain();
    assert(logged==1 && packets[0][1]==0xb0 && packets[0][2]==64 && packets[0][3]==127);
    samples[SYN_SPACE]=3600; frame(); drain(); assert(logged==1);
    samples[SYN_SPACE]=3601; frame(); drain(); assert(logged==2 && !packets[1][3]);
    lighting_travel_frame(SYN_PROFILE,samples,lo,hi,true,rgb); keyboard_midi_lights(&midi,rgb,now);
    assert(rgb[SYN_SPACE*3]==0 && rgb[SYN_SPACE*3+1]==0 && rgb[SYN_SPACE*3+2]==255);
    samples[SYN_TAB]=3499; frame(); /* trigger: window starts at this readback */
    for(unsigned i=0;i<9;++i) { samples[SYN_TAB]=3400-i*100; frame(); }
    assert(fabsf(raw.velocity[SYN_TAB].value-200000.0f/4500000.0f)<0.000001f);
    drain(); assert(midi.refs[72]==1);
    samples[SYN_TAB]=3900; frame(); drain(); assert(!midi.refs[72]);
    chord(SYN_SHIFT); assert(midi.lower_muted);
    lighting_travel_frame(SYN_PROFILE,samples,lo,hi,true,rgb); keyboard_midi_lights(&midi,rgb,now);
    assert(rgb[100*3]==0 && rgb[100*3+1]==0 && rgb[100*3+2]==0);
    chord(SYN_S); assert(menu.music_page==MENU_SCALE);
    samples[SYN_H]=3000; frame(); samples[SYN_H]=3900; frame(); frame(); drain();
    assert(midi.music.scale==MIDI_SCALE_PHRYGIAN);
    chord(SYN_E); assert(menu.music_page==MENU_KEY);
    samples[SYN_TAB]=3000; frame(); samples[SYN_TAB]=3900; frame(); frame(); drain();
    assert(midi.music.root==0);
}
static void calibration(void)
{
    keyboard_calibration_t cal; calibration_init(&cal);
    assert(!calibration_start(&cal,1,SYN_COUNT,0));
    assert(calibration_start(&cal,SYN_PROFILE,SYN_COUNT,0));
    for(unsigned i=0;i<SYN_COUNT;++i) samples[i]=4000;
    calibration_frame(&cal,samples,true,true,0);
    calibration_frame(&cal,samples,true,true,500);
    assert(cal.state==CAL_COLLECT);
    for(unsigned i=0;i<SYN_COUNT;++i) samples[i]=1000;
    calibration_frame(&cal,samples,true,false,501);
    calibration_lights(&cal,rgb,501);
    assert(rgb[103*3]==128 && rgb[103*3+1]==48);
    calibration_frame(&cal,samples,true,false,1501);
    assert(cal.completed==SYN_COUNT && cal.state==CAL_SAVE);
    assert(calibration_bounds_valid(SYN_PROFILE,SYN_COUNT,cal.lower,cal.upper));
}
static void lifecycle(void)
{
    init();
    samples[100]=3000; frame();
    keyboard_app_service(&app,now,true,send_hid,send);
    assert(keyboard_report_get_usage(&hid,4));
    refuse_output=true; samples[100]=3900; frame();
    keyboard_app_service(&app,now,true,send_hid,send);
    assert(keyboard_report_get_usage(&hid,4)); /* pending transfer immutable */
    refuse_output=false; keyboard_app_service(&app,now,true,send_hid,send);
    assert(!keyboard_report_get_usage(&hid,4));
    samples[100]=3000; frame(); keyboard_app_service(&app,now,true,send_hid,send);
    now+=100; keyboard_app_service(&app,now,true,send_hid,send);
    assert(!raw.armed && !keyboard_report_get_usage(&hid,4));
    keyboard_app_lights(&app,lo,hi,rgb,now);
    for(unsigned i=0;i<sizeof(rgb);++i) assert(!rgb[i]);
    frame(); assert(!raw.armed);
    samples[100]=3900; frame(); assert(raw.armed);
    assert(keyboard_app_calibrate(&app,now,true));
    frame(); now+=500; frame(); assert(app_cal.state==CAL_COLLECT);
    for(unsigned i=0;i<SYN_COUNT;++i) samples[i]=1000;
    frame(); now+=1000; frame();
    assert(app_cal.state==CAL_DONE && saved==1 && !raw.armed);
    assert(lo[103]==1000 && hi[103]==3900);
    keyboard_app_service(&app,now,true,send_hid,send);
    assert(!keyboard_report_get_usage(&hid,4));
    keyboard_app_invalidate(&app,now);
    assert(!app.sent_valid && !raw.armed);
    keyboard_app_frame(&app,NULL,SYN_COUNT,SYN_PROFILE,lo,hi,true,now);
    assert(!raw.valid);
}
static void deferred_candidate(void)
{
    init(); save_result=KEYBOARD_SAVE_DEFER;
    assert(keyboard_app_calibrate(&app,now,true));
    frame(); now+=CALIBRATION_SETTLE_MS; frame();
    assert(app_cal.state==CAL_COLLECT);
    for(unsigned i=0;i<SYN_COUNT;++i) samples[i]=1000;
    frame(); now+=CALIBRATION_HOLD_MS; frame();
    assert(app_cal.state==CAL_SAVE && saved==1);
    for(unsigned i=0;i<SYN_COUNT;++i) {
        assert(lo[i]==1000 && hi[i]==4000);
        assert(app_cal.lower[i]==1000 && app_cal.upper[i]==3900);
    }
}
static void deferred_calibration(void)
{
    deferred_candidate();
    uint32_t activity=app_cal.activity;
    for(unsigned i=0;i<5;++i) {
        frame(); keyboard_app_service(&app,now,true,send_hid,send);
        assert(app_cal.state==CAL_SAVE && app_cal.activity==activity);
        assert(hi[103]==4000 && saved==i+2);
        const keyboard_report_t empty={0}; assert(!memcmp(&hid,&empty,sizeof(hid)));
    }
    save_result=KEYBOARD_SAVE_COMPLETE; frame();
    assert(app_cal.state==CAL_DONE && saved==7 && !raw.armed);
    for(unsigned i=0;i<SYN_COUNT;++i) assert(lo[i]==1000 && hi[i]==3900);
    frame(); assert(saved==7); /* no retry after success */
    for(unsigned i=0;i<SYN_COUNT;++i) samples[i]=3900;
    frame(); assert(raw.armed);

    /* Failure, bad input, timeout and cancellation must never publish a
     * pending candidate or call the backend after the candidate is gone. */
    for(unsigned scenario=0;scenario<9;++scenario) {
        deferred_candidate();
        uint32_t ack=0; uint8_t result=0;
        if(scenario==0 || scenario==1) {
            save_result=scenario==0?KEYBOARD_SAVE_FAILED:(keyboard_save_result_t)99;
            frame();
            assert(app_cal.state==CAL_ERROR && app_cal.reason==CAL_STORAGE && saved==2);
        } else {
            if(scenario==2) { now=app_cal.activity+CALIBRATION_IDLE_MS; frame(); }
            if(scenario==3) { samples[103]=0; frame(); }
            if(scenario==4) { samples[103]=4097; frame(); }
            if(scenario==5) {
                assert(keyboard_app_command(&app,"cfg calcancel 1",now,true,&ack,&result));
                assert(ack==1 && result==1);
            }
            if(scenario==6) keyboard_app_service(&app,app.last_frame+SCAN_STALE_MS,true,send_hid,send);
            if(scenario==7) keyboard_app_frame(&app,samples,SYN_COUNT,SYN_PROFILE,lo,hi,false,now++);
            if(scenario==8) keyboard_app_invalidate(&app,now);
            assert(app_cal.state==CAL_ABORTED && saved==1);
            assert(app_cal.reason==(scenario==2?CAL_TIMEOUT:scenario==5?CAL_CANCELLED:CAL_INVALID));
        }
        assert(!app_cal.completed && !calibration_active(&app_cal));
        for(unsigned i=0;i<SYN_COUNT;++i) {
            assert(lo[i]==1000 && hi[i]==4000);
            assert(!app_cal.lower[i] && !app_cal.upper[i]);
        }
        unsigned attempts=saved; frame(); assert(saved==attempts);
    }
    deferred_candidate();
    uint16_t small[7]={3900,3900,3900,3900,3900,3900,3900};
    keyboard_app_frame(&app,small,7,43,lo,hi,true,now++);
    assert(!calibration_active(&app_cal) && !app_cal.completed && saved==1);
    assert(lo[103]==1000 && hi[103]==4000);
}
static void commands(void)
{
    init(); uint32_t ack=10; uint8_t result=9;
    assert(!keyboard_app_command(&app,"unknown",now,true,&ack,&result));
    assert(keyboard_app_command(&app,"cfg get 4294967296",now,true,&ack,&result));
    assert(ack==10 && result==9);
    assert(keyboard_app_command(&app,"cfg set 11 103 3000 3200",now,true,&ack,&result));
    assert(ack==11 && result==1 && raw.press[103]==3000 && !raw.armed);
    frame(); assert(raw.armed);
    assert(keyboard_app_command(&app,"cfg midi 12 103 70",now,true,&ack,&result));
    assert(result==1 && midi.mapping[103]==70);
    assert(keyboard_app_command(&app,"cfg midi 13 3 70",now,true,&ack,&result));
    assert(result==2 && midi.mapping[SYN_SPACE]==255);
    assert(keyboard_app_command(&app,"cfg key 14 103 135",now,true,&ack,&result));
    assert(result==1 && raw.keycode[103]==135 && !raw.armed);
    const char *badmap[]={"cfg key 14","cfg key 14 103","cfg key 14 104 4",
                         "cfg key 14 103 3","cfg key 14 103 232","cfg key 14 103 256",
                         "cfg key 14 103 4294967296","cfg key 14 103 4 junk"};
    for(unsigned i=0;i<sizeof(badmap)/sizeof(badmap[0]);++i) {
        assert(keyboard_app_command(&app,badmap[i],now,true,&ack,&result));
        assert(result==2 && raw.keycode[103]==135);
    }
    const char *bad[]={"cfg set 14","cfg set 14 3","cfg set 14 3 3000",
                      "cfg set 14 104 3000 3200","cfg set 14 103 3200 3000",
                      "cfg all 14 3000 4096","cfg all 14 3000 3200 x"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        assert(keyboard_app_command(&app,bad[i],now,true,&ack,&result));
        assert(result==2 && raw.press[103]==3000);
    }
    /* Cold boot: the host clears the store exactly like Fn+R, and defaults
     * follow on the next neutral frame. */
    assert(resets==0);
    assert(keyboard_app_command(&app,"cfg clean 15",now,true,&ack,&result));
    assert(ack==15 && result==1 && resets==1 && app.reset_pending);
    /* Defaults wait for a neutral frame; a held key keeps the reset pending. */
    samples[103]=1000; frame(); assert(app.reset_pending);
    samples[103]=3900;
    frame(); assert(!app.reset_pending && raw.press[103]==RAW_DEFAULT_PRESS);
    for(unsigned i=0;i<4 && !raw.armed;++i) frame();
    assert(raw.armed);                       /* fresh defaults re-arm on neutral input */
    assert(keyboard_app_command(&app,"cfg clean 16 trailing",now,true,&ack,&result));
    assert(ack==16 && result==2 && resets==1);
    assert(keyboard_app_command(&app,"cfg clean 17",now,true,&ack,&result));
    assert(ack==17 && result==1 && resets==2);
}
static void layout_change(void)
{
    init(); assert(keyboard_app_calibrate(&app,now,true)); frame();
    assert(calibration_active(&app_cal));
    uint16_t small[7]={3900,3900,3900,3900,3900,3900,3900};
    keyboard_app_frame(&app,small,7,43,lo,hi,true,now++);
    assert(raw.count==7 && raw.profile==43 && !calibration_active(&app_cal) && !saved);
    assert(loaded==2);
    synthetic_rate(0); assert(!keyboard_layout_count(SYN_PROFILE));
    keyboard_app_frame(&app,samples,SYN_COUNT,SYN_PROFILE,lo,hi,true,now);
    assert(!raw.valid);
}
static void reset_while_held(void)
{
    init();
    uint32_t ack=0; uint8_t result=0;
    samples[100]=3000; frame();
    keyboard_app_service(&app,now,true,send_hid,send);
    assert(raw.armed && keyboard_report_get_usage(&hid,4));
    assert(keyboard_app_command(&app,"cfg clean 1",now,true,&ack,&result));
    assert(result==1 && app.reset_pending && !raw.armed);
    keyboard_app_service(&app,now,true,send_hid,send);
    assert(!keyboard_report_get_usage(&hid,4));
    for(unsigned i=0;i<10;++i) { frame(); assert(app.reset_pending); }
    samples[100]=3900; frame(); assert(!app.reset_pending);
    frame(); assert(raw.armed);
    now+=100;
    assert(keyboard_app_command(&app,"cfg clean 2",now,true,&ack,&result));
    assert(result==2 && resets==1); /* stale scans never authorize an erase */
    keyboard_app_invalidate(&app,now);
    assert(keyboard_app_command(&app,"cfg clean 3",now,true,&ack,&result));
    assert(result==2 && resets==1);
    init(); keyboard_raw_enable(&raw,false); frame();
    assert(!raw.enabled && raw.valid && !raw.armed);
    assert(keyboard_app_command(&app,"cfg clean 4",now,true,&ack,&result));
    assert(result==1 && app.reset_pending);
    frame(); assert(!app.reset_pending && raw.enabled);
}
static void atomic_press_edit(void)
{
    init();
    uint16_t before[SYN_COUNT]; memcpy(before,raw.press,sizeof(before));
    raw.release[SYN_COUNT-1]=1;
    assert(!keyboard_raw_set_press_all(&raw,2000));
    assert(!memcmp(before,raw.press,sizeof(before)) && raw.revision==0 && raw.armed);
    raw.release[SYN_COUNT-1]=3600;
    assert(keyboard_raw_set_press_all(&raw,4095));
    for(unsigned i=0;i<SYN_COUNT;++i) assert(raw.press[i]==3599);
    assert(raw.revision==1 && !raw.armed);
}
static void unavailable_storage(void)
{
    init();keyboard_app_init(&app,&raw,&midi,&menu,&app_cal,NULL);frame();
    assert(!keyboard_app_calibrate(&app,now,true));
    assert(menu.disabled_options&(1u<<(MENU_CALIBRATION-1u)));
    assert(menu.disabled_options&(1u<<(MENU_RESET-1u)));
    assert(!keyboard_app_reset_profile(&app));
    /* Restore callbacks: capability filtering must not leak across init. */
    init();assert(!menu.disabled_options && keyboard_app_calibrate(&app,now,true));
}
static void sensor_readback(void)
{
    init();frame();
    uint8_t out[MT_BOUNDS_SIZE(SYN_COUNT)];
    assert(keyboard_bounds_encode(&app,now,out,sizeof(out))==sizeof(out));
    assert(!memcmp(out,"MTB1",4) && out[5]==SYN_PROFILE && out[6]==SYN_COUNT && out[7]==1);
    assert(app.readback[103].sample==3900 && app.readback[103].control==3900);
    samples[103]=1234;lo[103]=999;hi[103]=3999;
    assert(app.readback[103].sample==3900 && app.readback[103].lower==1000 && app.readback[103].upper==4000);
    assert(!keyboard_bounds_encode(&app,now,out,sizeof(out)-1));
    assert(keyboard_bounds_encode(&app,now+SCAN_STALE_MS,out,sizeof(out)) && !(out[7]&1));
    keyboard_app_invalidate(&app,now);
    assert(keyboard_bounds_encode(&app,now,out,sizeof(out)) && !(out[7]&1));
    frame();assert(app.readback[103].sample==1234 && app.readback[103].lower==999);
}
static void calibration_observation(void)
{
    init();assert(keyboard_app_calibrate(&app,now,true));
    frame();now+=CALIBRATION_SETTLE_MS;frame();
    assert(app_cal.state==CAL_COLLECT && raw.valid && !raw.armed && raw.neutral_idle);
    samples[100]=samples[103]=1000;
    for(unsigned i=0;i<=CALIBRATION_HOLD_MS;++i) {
        frame();keyboard_app_service(&app,now,true,send_hid,send);
        assert(raw.valid && !raw.armed && !raw.neutral_idle);
        assert(app.readback[100].sample==1000 && app.readback[103].control==1000);
        for(unsigned k=0;k<SYN_COUNT;++k)
            assert(!raw.down[k] && !raw.velocity[k].captures && !raw.velocity[k].pending);
        assert(!keyboard_report_get_usage(&hid,4));
    }
    assert(app_cal.completed==2 && app_cal.lower[100]==1000 && app_cal.lower[103]==1000);
    samples[100]=samples[103]=3900;frame();assert(raw.neutral_idle && !raw.armed);
    samples[10]=0;frame();
    assert(app_cal.state==CAL_ABORTED && !raw.valid && !raw.armed && !saved);
    samples[10]=3900;frame();assert(raw.armed);
    samples[100]=3000;frame();assert(keyboard_report_get_usage(&raw.engine.report,4));
}
int main(void)
{
    calibration_observation();
    sensor_readback();
    normalizer(); performance(); calibration(); lifecycle(); deferred_calibration(); commands(); layout_change(); reset_while_held(); atomic_press_edit(); unavailable_storage();
    puts("PASS SDK-free application: 104 keys, opaque IDs/layout, 2kHz velocity, 16-bit ascending ADC, linear LEDs, HID/MIDI/sustain/menus/scales, parallel calibration");
}
