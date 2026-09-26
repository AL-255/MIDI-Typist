#include "keyboard_menu.h"
#include "travel_lighting.h"
#include "huntsman_layout.h"
#include "keyboard_scan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static keyboard_raw_t raw;
static keyboard_menu_t menu;
static uint16_t samples[65], lower[65], upper[65];
static uint32_t now;
static uint8_t last_action;
static void frame(void)
{
    keyboard_config_t before=raw.engine.config;
    keyboard_raw_frame(&raw,samples,raw.count,raw.profile,true);
    last_action=keyboard_menu_frame(&menu,&raw,lower,upper,&before,now,false,false,NULL,1u);
    ++now;
}
static void init(unsigned profile)
{
    keyboard_raw_init(&raw); keyboard_menu_init(&menu);
    raw.menu_managed=true;
    raw.count=profile==3?65:60+profile; raw.profile=profile;
    for (unsigned i=0; i<65; ++i) { samples[i]=4000; lower[i]=1000+i; upper[i]=4000; }
    keyboard_raw_invalidate(&raw); now=1000; frame();
}
static unsigned sensor(unsigned key)
{
    for (unsigned i=0; i<raw.count; ++i) if (menu.keys[i]==key) return i;
    assert(0); return 0;
}
static void key(unsigned key, bool down)
{
    samples[sensor(key)]=down?500:4000; frame();
}
static void rgb(unsigned i, const uint8_t *p, unsigned r, unsigned g, unsigned b)
{
    const lighting_channels_t *c=&g_lighting_channels[raw.profile-1][i];
    p+=c->controller*192u;
    assert(p[c->red]==r && p[c->green]==g && p[c->blue]==b);
}
static void light_effect(void)
{
    init(1);
    assert(menu.effect==KEYBOARD_LIGHT_WHITE);
    key(KEY_ID_FN,true);
    key(0x1du,true); /* physical backslash */
    assert(menu.pending==MENU_LIGHT_EFFECT && menu.effect==KEYBOARD_LIGHT_WHITE);
    key(0x1du,false);
    assert(last_action==MENU_LIGHT_EFFECT && menu.effect==KEYBOARD_LIGHT_RAINBOW);
    key(KEY_ID_FN,false);frame();
    key(KEY_ID_FN,true);key(0x1du,true);
    assert(menu.pending==MENU_LIGHT_EFFECT);
    key(0x1du,false);
    assert(last_action==MENU_LIGHT_EFFECT && menu.effect==KEYBOARD_LIGHT_WHITE);
    key(KEY_ID_FN,false);
}
static void thresholds(void)
{
    for (unsigned profile=1; profile<=3; ++profile) {
        init(profile);
        for (unsigned level=1; level<=10; ++level) {
            raw.engine.config.saved_actuation=level;
            assert(keyboard_menu_thresholds(&raw,lower,upper));
            assert(!raw.armed && raw.engine.config.saved_actuation==level);
            for (unsigned i=0; i<raw.count; ++i) {
                optical_key_config_t c;
                keyboard_scan_thresholds(&raw.engine.config,menu.keys[i],&c);
                assert(raw.press[i]>0 && raw.press[i]<raw.release[i] && raw.release[i]<4096);
                for (unsigned sample=1; sample<=4096; ++sample) {
                    unsigned travel=optical_key_level(lower[i],upper[i],sample);
                    assert((sample<raw.press[i])==(travel>c.press));
                    assert((sample>raw.release[i])==(travel<c.release));
                }
            }
            frame(); assert(raw.armed && raw.engine.config.saved_actuation==level);
        }
        uint32_t rev=raw.revision; uint16_t old=raw.press[0];
        upper[raw.count-1]=10;
        assert(!keyboard_menu_thresholds(&raw,lower,upper));
        assert(raw.press[0]==old && raw.revision==rev);
    }
}
static void editor(void)
{
    init(1);
    samples[menu.fn]=samples[menu.tab]=500; frame(); /* same scan, Tab index before Fn */
    assert(raw.engine.config.mode==KEY_CONFIG_NORMAL && menu.pending==MENU_TRIGGER);
    key(KEY_ID_TAB,false); key(KEY_ID_FN,false);
    assert(raw.engine.config.mode==KEY_CONFIG_ACTUATION);
    key(0x0b,true); key(0x0b,false);
    assert(raw.engine.config.actuation==10 && raw.revision==0);
    uint8_t leds[LIGHTING_FRAME_SIZE];
    memset(leds,7,sizeof(leds)); menu.brightness=0;
    keyboard_menu_lights(&menu,&raw,lower,upper,leds,now,false,false,false);
    rgb(sensor(0x0b),leds,0,255,0); rgb(sensor(2),leds,25,25,25);
    rgb(sensor(KEY_ID_ESC),leds,255,0,0); rgb(menu.enter,leds,0,0,0);
    key(KEY_ID_ESC,true);
    assert(raw.engine.config.mode==0 && raw.engine.config.saved_actuation==10);
    assert(raw.revision==1 && !raw.armed && !keyboard_report_get_usage(&raw.engine.report,0x29));
    key(KEY_ID_ESC,false); assert(raw.armed);
    assert(raw.press[32]<1500); /* deep setting is effective, not just a menu label */
    keyboard_raw_enable(&raw,false); keyboard_raw_enable(&raw,true); frame();
    assert(raw.engine.config.saved_actuation==10);
    key(KEY_ID_FN,true); key(KEY_ID_TAB,true);
    assert(raw.engine.config.actuation==10);
    key(KEY_ID_TAB,false); key(KEY_ID_FN,false); key(2,true); key(2,false);
    /* A fault cancels a dirty edit without changing the last committed pair. */
    uint16_t saved=raw.press[32];
    keyboard_raw_invalidate(&raw); samples[menu.fn]=4000; frame();
    assert(raw.engine.config.saved_actuation==10 && raw.press[32]==saved && raw.revision==1);
    key(KEY_ID_FN,true); key(KEY_ID_TAB,true); key(KEY_ID_TAB,false); key(KEY_ID_FN,false);
    key(2,true); key(2,false); key(KEY_ID_FN,true); key(KEY_ID_TAB,true); /* editor commit */
    assert(!raw.engine.config.mode && raw.engine.config.saved_actuation==1 && raw.revision==2);
}
static void brightness_and_hints(void)
{
    for (unsigned profile=1; profile<=3; ++profile) {
        init(profile);
        key(KEY_ID_FN,true);
        uint8_t leds[LIGHTING_FRAME_SIZE];
        memset(leds,99,sizeof(leds));
        keyboard_menu_lights(&menu,&raw,lower,upper,leds,now,false,false,false);
        for (unsigned i=0; i<raw.count; ++i) {
            unsigned v=(i==menu.c || i==menu.tab || i==menu.k || i==menu.l || i==menu.caps || i==menu.r ||
                i==menu.option_sensors[MENU_LIGHT_EFFECT-1u])?255:0;
            rgb(i,leds,v,keyboard_shortcut_usage(profile,menu.keys[i])?255:v,i==menu.enter?255:v);
        }
        samples[menu.k]=500; frame(); assert(menu.brightness==19 && menu.pending==MENU_LIGHT_DOWN);
        now+=5000; frame(); assert(menu.brightness==19); /* no hold repeat */
        key(KEY_ID_FN,false); assert(menu.brightness==18 && !menu.pending);
        key(KEY_ID_FN,true); assert(menu.brightness==18);
        samples[menu.k]=4000; frame(); key(KEY_ID_FN,false);
        for (unsigned i=0; i<25; ++i) {
            key(KEY_ID_FN,true); samples[menu.k]=500; frame();
            samples[menu.k]=4000; frame(); key(KEY_ID_FN,false);
        }
        assert(menu.brightness==0 && keyboard_menu_brightness(&menu)==0);
        key(KEY_ID_FN,true);
        memset(leds,99,sizeof(leds));
        keyboard_menu_lights(&menu,&raw,lower,upper,leds,now,true,false,false);
        /* Tab is a MIDI-mode hint too now: it opens the raw trigger page. */
        rgb(menu.l,leds,25,25,25); rgb(menu.tab,leds,25,25,25); rgb(menu.c,leds,0,0,0);
        samples[menu.l]=500; now=UINT32_MAX-100; frame();
        assert(menu.brightness==0 && menu.pending==MENU_LIGHT_UP);
        now=149; frame(); assert(menu.brightness==0);
        samples[menu.l]=4000; frame(); assert(menu.brightness==1);
        key(KEY_ID_FN,false);
        for (unsigned i=0; i<25; ++i) {
            key(KEY_ID_FN,true); samples[menu.l]=500; frame();
            samples[menu.l]=4000; frame(); key(KEY_ID_FN,false);
        }
        assert(menu.brightness==19);
        menu.brightness=0; key(KEY_ID_FN,false);
        memset(leds,99,sizeof(leds)); keyboard_menu_lights(&menu,&raw,lower,upper,leds,now,false,false,false);
        for (unsigned i=0; i<sizeof(leds); ++i) assert(!leds[i]);
        memset(leds,99,sizeof(leds)); keyboard_menu_lights(&menu,&raw,lower,upper,leds,now,false,true,false);
        for (unsigned i=0; i<sizeof(leds); ++i) assert(leds[i]==99);
    }
}
static unsigned hid_sensor(unsigned usage,unsigned modifier)
{
    for (unsigned i=0;i<raw.count;++i) {
        const keyboard_action_t *a=keyboard_action(raw.profile,menu.keys[i],0);
        if (a && a->type==2 && a->arg0==modifier && a->arg1==usage) return i;
    }
    assert(0); return 0;
}
static void application_keys(void)
{
    const unsigned shortcuts[][2]={{0x29,0x35},{0x1e,0x3a},{0x1f,0x3b},{0x20,0x3c},
        {0x21,0x3d},{0x22,0x3e},{0x23,0x3f},{0x24,0x40},{0x25,0x41},
        {0x26,0x42},{0x27,0x43},{0x2d,0x44},{0x2e,0x45},{0x2a,0x4c},
        {0x1c,0x49},{0x13,0x46},{0x11,0x4d},{0x10,0x4e},{0x0b,0x4a},{0x0d,0x4b}};
    const unsigned arrows[][3]={{0,64,0x50},{0x65,0,0x51},{0,16,0x4f},{0,32,0x52}};
    for (unsigned profile=1;profile<=3;++profile) {
        init(profile);
        for (unsigned i=0;i<4;++i) {
            if (profile==3 && i==1) continue; /* JIS has no Menu sensor. */
            samples[hid_sensor(arrows[i][0],arrows[i][1])]=500;
        }
        frame(); assert(!raw.engine.report.modifiers);
        for (unsigned i=0;i<4;++i)
            assert(keyboard_report_get_usage(&raw.engine.report,arrows[i][2]) == !(profile==3 && i==1));
        assert(!keyboard_report_get_usage(&raw.engine.report,0x65));
        for (unsigned i=0;i<raw.count;++i) samples[i]=4000;
        frame();
        for (unsigned i=0;i<sizeof(shortcuts)/sizeof(shortcuts[0]);++i) for(unsigned release=0;release<2;++release) {
            unsigned at=hid_sensor(shortcuts[i][0],0), usage=shortcuts[i][1];
            assert(keyboard_shortcut_usage(profile,menu.keys[at])==usage);
            samples[menu.fn]=samples[at]=500; frame(); /* same scan, Fn must win */
            assert(keyboard_report_get_usage(&raw.engine.report,usage));
            assert(!keyboard_report_get_usage(&raw.engine.report,shortcuts[i][0]) && !menu.pending);
            samples[release?menu.fn:at]=4000; frame();
            assert(!keyboard_report_get_usage(&raw.engine.report,usage));
            assert(!keyboard_report_get_usage(&raw.engine.report,shortcuts[i][0]));
            samples[menu.fn]=samples[at]=4000; frame();
        }
        unsigned y=hid_sensor(0x1c,0);
        samples[y]=500; frame(); key(KEY_ID_FN,true); /* no reinterpretation of preheld Y */
        assert(keyboard_report_get_usage(&raw.engine.report,0x1c) && !keyboard_report_get_usage(&raw.engine.report,0x49));
        samples[y]=4000; frame();
        for(unsigned tap=0;tap<3;++tap) {
            samples[y]=500; frame(); assert(keyboard_report_get_usage(&raw.engine.report,0x49));
            samples[y]=4000; frame(); assert(!keyboard_report_get_usage(&raw.engine.report,0x49));
        }
        key(KEY_ID_FN,false);
        samples[hid_sensor(0,2)]=500; samples[hid_sensor(0,64)]=500; frame();
        assert(raw.engine.report.modifiers==2 && keyboard_report_get_usage(&raw.engine.report,0x50));
        keyboard_raw_invalidate(&raw); assert(!raw.engine.report.modifiers && !keyboard_report_get_usage(&raw.engine.report,0x50));
    }
}
static void repeated_brightness(void)
{
    for (unsigned profile=1; profile<=3; ++profile) for (unsigned midi=0; midi<2; ++midi) {
        init(profile); raw.midi_mode=midi; menu.brightness=10;
        key(KEY_ID_FN,true);
        /* Fn can remain within its hysteresis band throughout all taps. */
        samples[menu.fn]=raw.release[menu.fn]; frame();
        for (unsigned i=0; i<6; ++i) {
            samples[menu.k]=500; frame(); assert(menu.pending==MENU_LIGHT_DOWN);
            samples[menu.k]=raw.release[menu.k]; frame(); assert(!last_action);
            samples[menu.k]++; frame();
            assert(last_action==MENU_LIGHT_DOWN && menu.brightness==9 && !raw.armed);
            frame(); assert(menu.brightness_session && !menu.pending && !raw.armed);
            uint8_t leds[LIGHTING_FRAME_SIZE]; memset(leds,99,sizeof(leds));
            keyboard_menu_lights(&menu,&raw,lower,upper,leds,now,midi,false,false);
            rgb(menu.k,leds,56,56,56); rgb(menu.l,leds,56,56,56);
            samples[menu.l]=500; frame(); assert(menu.pending==MENU_LIGHT_UP);
            samples[menu.l]=4000; frame(); assert(last_action==MENU_LIGHT_UP && menu.brightness==10);
        }
        /* The exception must never authorize RESET or mode switching. */
        samples[menu.r]=500; frame(); assert(!menu.pending && !last_action);
        samples[menu.r]=4000; samples[menu.enter]=500; frame(); assert(!menu.pending && !last_action);
        samples[menu.enter]=4000; frame();
        ++raw.revision; frame(); assert(!menu.brightness_session);
        samples[menu.k]=500; frame(); assert(!menu.pending);
        samples[menu.k]=4000; key(KEY_ID_FN,false); assert(raw.armed);
        key(KEY_ID_FN,true); samples[menu.k]=500; frame();
        key(KEY_ID_FN,false); assert(last_action==MENU_LIGHT_DOWN && !menu.brightness_session);
        key(KEY_ID_FN,true); samples[menu.k]=4000; frame();
        samples[menu.k]=500; frame(); assert(!menu.pending); /* Fn lift ends session */
    }
    init(1); key(KEY_ID_FN,true);
    samples[menu.k]=500; frame(); samples[menu.l]=500; frame();
    samples[menu.k]=4000; frame(); frame(); assert(!menu.pending); /* preheld L isn't a tap */
    samples[menu.l]=4000; frame(); samples[menu.l]=500; frame(); assert(menu.pending==MENU_LIGHT_UP);
    keyboard_menu_cancel(&menu); frame(); assert(!menu.pending && !menu.brightness_session);
}
static void reset_confirmation(void)
{
    for (unsigned profile=1; profile<=3; ++profile) for (unsigned midi=0; midi<2; ++midi)
        for (unsigned choice=0; choice<3; ++choice) {
            init(profile); raw.midi_mode=midi;
            samples[menu.fn]=samples[menu.r]=samples[menu.y]=500; frame();
            samples[menu.r]=4000; frame();
            assert(menu.reset_confirmation && !last_action && !menu.confirmation_ready);
            assert(menu.text.length==6);
            uint8_t leds[LIGHTING_FRAME_SIZE]; menu.brightness=0;
            keyboard_menu_lights(&menu,&raw,lower,upper,leds,now,midi,false,false);
            rgb(menu.y,leds,0,255,0); rgb(menu.n,leds,255,0,0);
            key(KEY_ID_FN,false); frame(); assert(!last_action && !menu.confirmation_ready);
            samples[menu.y]=4000; frame(); assert(menu.confirmation_ready && !last_action);
            samples[menu.y]=choice!=1?500:4000; samples[menu.n]=choice!=0?500:4000; frame();
            assert(last_action==(choice==0?MENU_RESET:MENU_NONE));
            assert(!menu.reset_confirmation && !menu.text.length && !raw.armed);
            for (unsigned usage=0; usage<256; ++usage) assert(!keyboard_report_get_usage(&raw.engine.report,usage));
            frame(); assert(!last_action);
        }
    for (unsigned fault=0; fault<5; ++fault) {
        init(1); samples[menu.fn]=samples[menu.r]=500; frame();
        samples[menu.fn]=samples[menu.r]=4000; frame(); frame(); assert(menu.confirmation_ready);
        if (fault==0) ++raw.revision;
        if (fault==1) samples[0]=0;
        if (fault==2) keyboard_raw_enable(&raw,false);
        if (fault==3) keyboard_menu_cancel(&menu);
        if (fault==4) {
            keyboard_config_t before=raw.engine.config;
            assert(!keyboard_menu_frame(&menu,&raw,lower,upper,&before,now,true,false,NULL,1u));
        }
        frame(); assert(!menu.reset_confirmation && !last_action);
        samples[menu.y]=500; frame(); assert(!last_action);
    }
}
int main(void)
{
    light_effect();
    thresholds(); editor(); brightness_and_hints(); application_keys(); repeated_brightness(); reset_confirmation();
    for (unsigned profile=1;profile<=3;++profile) for (unsigned action=1;action<=MENU_RESET;++action)
        for (unsigned release=0;release<2;++release) {
            init(profile);
            const unsigned choices[]={menu.c,menu.tab,menu.enter,menu.k,menu.l,menu.caps,menu.r};
            unsigned choice=choices[action-1];
            samples[menu.fn]=samples[choice]=500; frame();
            assert(!last_action && menu.pending==action && !raw.engine.config.mode && !raw.armed);
            now+=4000; frame(); assert(!last_action && menu.pending==action && menu.brightness==19);
            samples[release?menu.fn:choice]=4000; frame();
            assert(last_action==(action==MENU_RESET?MENU_NONE:action) && !menu.pending);
            assert(menu.reset_confirmation==(action==MENU_RESET));
            assert((menu.text.length!=0)==(action==MENU_RESET));
            frame(); assert(!last_action);
            samples[menu.fn]=samples[choice]=4000; frame();
        }
    init(1); samples[menu.fn]=samples[menu.r]=500; frame(); assert(menu.pending==MENU_RESET);
    assert(keyboard_raw_set(&raw,0,3500,3700)); frame(); assert(!menu.pending && !last_action);
    samples[menu.fn]=4000; frame(); assert(!last_action); /* edit cancels reset */
    init(1); samples[menu.fn]=samples[menu.c]=samples[menu.r]=500; frame(); assert(!menu.pending);
    samples[menu.c]=4000; frame(); assert(!menu.pending && !last_action);
    init(1); raw.midi_mode=true;
    samples[menu.fn]=samples[menu.r]=500; frame(); assert(menu.pending==MENU_RESET);
    samples[menu.fn]=4000; frame(); assert(!last_action && menu.reset_confirmation);
    init(1); samples[menu.fn]=samples[menu.r]=500; frame();
    samples[0]=0; frame(); assert(!menu.pending && !last_action);
    init(1); samples[menu.fn]=samples[menu.r]=500; frame();
    keyboard_raw_enable(&raw,false); frame(); assert(!menu.pending && !last_action);
    puts("PASS menu: exact calibrated Schmitt inversion, commit/cancel/neutral, simultaneous Fn, hints, release actions/wrap across layouts");
    return 0;
}
