#include "keyboard_midi.h"
#include "keyboard_menu.h"
#include "huntsman_layout.h"
#include "travel_lighting.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static keyboard_raw_t raw;
static keyboard_midi_t midi;
static keyboard_menu_t menu;
static uint16_t values[65], lower[65], upper[65];
static uint8_t log_events[20000][4];
static unsigned logged, frames;
static bool blocked;
static bool send_event(uint8_t cin, uint8_t status, uint8_t note, uint8_t value)
{
    if (blocked) return false;
    assert(logged < 20000 && note < 128 && value < 128 && cin == status >> 4);
    uint8_t *p = log_events[logged++]; p[0]=cin; p[1]=status; p[2]=note; p[3]=value;
    return true;
}
static unsigned sensor(uint8_t usage, uint8_t modifier)
{
    for (unsigned i=0; i<raw.count; ++i) {
        const uint8_t key = keyboard_key_for_sensor(raw.profile,i);
        const keyboard_action_t *a = keyboard_action(raw.profile,key,0);
        if (a && key != KEY_ID_FN && a->type==2 && a->arg0==modifier && a->arg1==usage) return i;
    }
    assert(false); return 0;
}
static unsigned fn_sensor(void)
{
    for (unsigned i=0; i<raw.count; ++i)
        if (keyboard_key_for_sensor(raw.profile,i)==KEY_ID_FN) return i;
    assert(false); return 0;
}
static void step(void)
{
    keyboard_config_t before=raw.engine.config;
    keyboard_raw_frame(&raw,values,raw.count ? raw.count : 61,raw.profile ? raw.profile : 1,true);
    uint8_t action=keyboard_menu_frame(&menu,&raw,lower,upper,&before,frames/8,false,midi.lower_muted,&midi.music,midi.velocity_start);
    if (action==MENU_MODE)
        keyboard_midi_toggle(&midi,&raw,frames/8);
    if (action==MENU_LOWER) keyboard_midi_toggle_lower(&midi,&raw);
    if (action==MENU_JANKO) keyboard_midi_toggle_janko(&midi,&raw);
    if (action==MENU_VELOCITY_SET) keyboard_midi_set_velocity_start(&midi,menu.selection);
    if (action==MENU_SELECT_KEY) assert(keyboard_midi_select_music(&midi,&raw,menu.selection,midi.music.scale));
    if (action==MENU_SELECT_SCALE) assert(keyboard_midi_select_music(&midi,&raw,midi.music.root,menu.selection));
    keyboard_midi_frame(&midi,&raw,lower,upper,frames++/8);
}
static void drain(void)
{
    for (unsigned i=0; i<300; ++i) keyboard_midi_service(&midi,frames/8,send_event);
}
static void init(void)
{
    keyboard_raw_init(&raw); keyboard_midi_init(&midi);
    keyboard_menu_init(&menu); raw.menu_managed=true;
    logged=frames=0; blocked=false;
    for (unsigned i=0;i<65;++i) { values[i]=3900; lower[i]=1000; upper[i]=3900; }
    step(); assert(raw.armed && !midi.mode);
}
static void toggle(void)
{
    const unsigned fn=fn_sensor(), ent=sensor(0x28,0);
    const unsigned mode=midi.mode;
    values[fn]=values[ent]=2400; step();
    assert(midi.mode==mode && !raw.armed && menu.pending==MENU_MODE);
    for (unsigned i=0; i<20; ++i) step();
    assert(midi.mode==mode); /* holding previews without changing mode */
    values[fn]=values[ent]=3900;
    step(); assert(midi.mode==(mode^1));
    drain(); step(); logged=0;
}
static unsigned events(unsigned status,unsigned note);
static void janko_toggle(void)
{
    const unsigned fn=fn_sensor(), j=sensor(0x0d,0);
    const bool state=midi.janko;
    values[fn]=values[j]=2400; step();
    assert(midi.janko==state && !raw.armed && menu.pending==MENU_JANKO);
    for (unsigned i=0; i<20; ++i) step();
    assert(midi.janko==state); /* holding previews without toggling */
    values[fn]=values[j]=3900;
    step(); assert(midi.janko==!state);
    drain(); step(); logged=0;
}
static void janko_strike(unsigned usage, unsigned expected, unsigned modifier)
{
    /* Shift keys carry their modifier mask with a zero usage in the tables. */
    const unsigned key=sensor((uint8_t)(modifier ? 0 : usage),(uint8_t)modifier);
    logged=0;
    values[key]=2400; step();
    for (unsigned frame=0; frame<9; ++frame) { values[key]-=100; step(); }
    values[key]=1400; step(); /* bottom-out closes the window and fires the note */
    drain();
    assert(events(0x90,expected)==1);
    values[key]=3900; step(); drain();
    assert(events(0x80,expected)==1);
}

static unsigned events(unsigned status,unsigned note)
{
    unsigned n=0;
    for (unsigned i=0;i<logged;++i) if (log_events[i][1]==status && log_events[i][2]==note) ++n;
    return n;
}
static void press_fit(unsigned i)
{
    values[i]=3400; step(); /* trigger: the window starts at this readback */
    for (unsigned j=1;j<=9;++j) { values[i]=3400-j*100; step(); }
}
static void default_mapping(void)
{
    init();
    static const uint8_t expected[][2]={{0x2b,72},{0x14,74},{0x1a,76},{8,77},{0x15,79},
        {0x17,81},{0x1c,83},{0x18,84},{0x0c,86},{0x12,88},{0x13,89},{0x2f,91},
        {0x30,93},{0x31,95},{0x1e,73},{0x1f,75},{0x21,78},{0x22,80},{0x23,82},
        {0x25,85},{0x26,87},{0x2d,90},{0x2e,92},{0x2a,94},
        {4,61},{0x1d,62},{0x16,63},{0x1b,64},{6,65},{9,66},{0x19,67},{0x0a,68},
        {5,69},{0x0b,70},{0x11,71},{0x10,72},{0x0e,73},{0x36,74},{0x0f,75},
        {0x37,76},{0x38,77},{0x34,78}};
    unsigned mapped=0;
    for (unsigned i=0;i<61;++i) mapped += midi.mapping[i]!=255;
    assert(mapped==43);
    for (unsigned i=0;i<sizeof(expected)/sizeof(expected[0]);++i)
        assert(midi.mapping[sensor(expected[i][0],0)]==expected[i][1]);
    assert(midi.mapping[sensor(0,2)]==60);
    assert(!keyboard_midi_map(&midi,&raw,fn_sensor(),40));
    assert(!keyboard_midi_map(&midi,&raw,sensor(0,1),40));
    assert(!keyboard_midi_map(&midi,&raw,sensor(0,4),40));
    assert(!keyboard_midi_map(&midi,&raw,65,40));
    assert(!keyboard_midi_map(&midi,&raw,0,128));
}
static void velocity_pressure_and_modes(void)
{
    init(); const unsigned tab=sensor(0x2b,0);
    press_fit(tab); drain(); assert(!logged); /* standard keyboard, no MIDI */
    assert(keyboard_report_get_usage(&raw.engine.report,0x2b));
    values[tab]=3900; step(); toggle();
    press_fit(tab); drain(); assert(events(0x90,72)==1);
    assert(log_events[0][3]==23); /* 800000 / 4500000 * 127 rounded */
    for(unsigned i=0;i<80;++i) step();
    drain();
    assert(events(0xa0,72)==1);
    values[tab]=1000; step(); frames+=80; drain();
    assert(log_events[logged-1][1]==0xa0 && log_events[logged-1][3]==127);
    values[tab]=3600; step(); drain(); assert(!events(0x80,72));
    values[tab]=3601; step(); drain(); assert(events(0x80,72)==1);
    press_fit(tab); drain();
    toggle(); assert(!midi.mode && !raw.midi_mode);
    values[tab]=3900; step(); assert(raw.armed);
    uint8_t rgb[LIGHTING_FRAME_SIZE]={0};
    keyboard_midi_lights(&midi,rgb,midi.changed_at);
    const lighting_channels_t *c=&g_lighting_channels[0][sensor(0x28,0)];
    assert(rgb[c->green]==255 && rgb[c->blue]==0 && !menu.text.length);
}
static void short_taps_and_overlap(void)
{
    init(); toggle(); const unsigned tab=sensor(0x2b,0);
    for(unsigned i=0;i<6;++i) { values[tab]=i%2 ? 3900 : 2400; step(); }
    for(unsigned i=0;i<9;++i) step(); /* the ten-sample window closes the fit */
    drain(); assert(events(0x90,72)==3 && events(0x80,72)==3);
    for(unsigned i=0;i<logged;++i) if(log_events[i][1]==0x90) assert(log_events[i][3]>=1);
}
static void shift_and_filtered_strike(void)
{
    init(); const unsigned shift=sensor(0,2);
    values[shift]=2400; step();
    assert(raw.engine.report.modifiers==2);
    values[shift]=3900; step(); toggle();
    values[shift]=2400; step();
    /* The fall keeps the window above the bottom-out 1500; the rise back from
     * the 2400 trigger is the pop the median filter discards, leaving the
     * clean 100-counts/sample slope (800000 counts/s -> MIDI velocity 23). */
    const uint16_t points[]={3300,3200,3100,3000,2900,2800,2700,2600,2500};
    for(unsigned i=0;i<9;++i) {values[shift]=points[i]; step();}
    drain(); assert(events(0x90,60)==1 && log_events[0][3]==23);
    assert(raw.engine.report.modifiers==0);
    values[shift]=3900; step(); drain(); assert(events(0x80,60)==1);
}
static void octave_and_duplicates(void)
{
    init(); toggle(); unsigned tab=sensor(0x2b,0), q=sensor(0x14,0), up=sensor(0,16);
    press_fit(tab); drain();
    values[up]=2400; step(); for(unsigned i=0;i<10;++i) step();
    assert(midi.octave==1); values[up]=3900; step();
    values[tab]=3900; step(); drain(); assert(events(0x80,72)==1 && !events(0x80,84));
    press_fit(tab); drain(); assert(events(0x90,84)==1);
    assert(keyboard_midi_map(&midi,&raw,q,72)); drain(); values[tab]=3900; step(); logged=0;
    press_fit(tab); press_fit(q); drain(); assert(events(0x90,84)==1);
    values[tab]=3900; step(); drain(); assert(!events(0x80,84));
    values[q]=3900; step(); drain(); assert(events(0x80,84)==1);
    midi.octave=-7; logged=0; press_fit(tab); drain(); assert(!logged);
}
static void faults_and_backpressure(void)
{
    init(); toggle(); unsigned tab=sensor(0x2b,0);
    press_fit(tab); blocked=true; drain(); assert(!logged && midi.count==1);
    values[tab]=3900; step(); drain(); assert(midi.count==2);
    blocked=false; drain(); assert(events(0x90,72)==1 && events(0x80,72)==1);
    logged=0; blocked=true;
    for(unsigned i=0;i<70 && !midi.errors;++i) {
        press_fit(tab); values[tab]=3900; step();
    }
    assert(midi.errors==1 && midi.panic && !midi.count && !midi.refs[72]);
    /* Busy MIDI must still allow the mode chord and normal HID recovery. */
    values[tab]=3900; step();
    values[fn_sensor()]=values[sensor(0x28,0)]=2400; step(); assert(midi.mode);
    values[fn_sensor()]=values[sensor(0x28,0)]=3900; step(); assert(!midi.mode);
    blocked=false; drain(); assert(!midi.panic && events(0xb0,120)==1 && events(0xb0,123)==1);
    assert(events(0x80,72)==1);
    for(unsigned i=0;i<65;++i) values[i]=3900;
    step(); toggle(); press_fit(tab); drain();
    keyboard_raw_invalidate(&raw); keyboard_midi_guard(&midi,&raw); drain();
    assert(!midi.refs[72] && !midi.count);
}
/* One 800000 counts/s press (0.17778 -> uncompressed MIDI velocity 23). */
static unsigned strike_velocity(unsigned usage)
{
    const unsigned key=sensor((uint8_t)usage,0);
    logged=0;
    values[key]=3400; step();
    for (unsigned frame=0; frame<9; ++frame) { values[key]-=100; step(); }
    values[key]=1400; step(); /* bottom-out closes the window */
    drain();
    unsigned velocity=0;
    for (unsigned i=0;i<logged;++i) if (log_events[i][1]==0x90) velocity=log_events[i][3];
    values[key]=3900; step(); drain();
    return velocity;
}

static unsigned digit_sensor(unsigned level)
{
    for (unsigned i=0;i<raw.count;++i)
        if (keyboard_editor_digit(raw.profile,menu.keys[i])==level) return i;
    assert(false); return 0;
}

static void velocity_start_mode(void)
{
    init();
    /* The setting shapes MIDI output only, so the host may apply it while the
     * keyboard is still in keyboard mode: the GUI cannot toggle MIDI mode. */
    assert(midi.velocity_start==1 && !midi.mode);
    keyboard_midi_set_velocity_start(&midi,4);
    assert(midi.velocity_start==4);
    keyboard_midi_set_velocity_start(&midi,11);
    assert(midi.velocity_start==4);
    keyboard_midi_set_velocity_start(&midi,1);
    assert(midi.velocity_start==1);
    toggle();
    assert(midi.mode && midi.velocity_start==1); /* survives the mode switch */
    assert(strike_velocity(0x14)==23); /* level 1 transmits the measured value */
    /* Fn+V opens the ten-step page; 1 is 0%, 0 is 100%. */
    const unsigned fn=fn_sensor(), v=sensor(0x19,0);
    values[fn]=values[v]=2400; step();
    assert(menu.pending==MENU_VELOCITY);
    values[fn]=values[v]=3900; step();
    assert(menu.velocity_page && menu.selection==1 && !raw.armed);
    for (unsigned i=0;i<65;++i) values[i]=3900;
    step(); /* page entry requires a released state */
    const unsigned esc=sensor(0x29,0);
    /* Selecting a level applies immediately, without leaving the page. */
    for (unsigned level=1; level<=10; ++level) {
        const unsigned key=digit_sensor(level);
        values[key]=2400; step();
        assert(menu.velocity_page && midi.velocity_start==level);
        values[key]=3900; step();
    }
    assert(midi.velocity_start==10);
    /* The bar lights digits up to the selection, green on the selection. */
    uint8_t frame[LIGHTING_FRAME_SIZE];
    memset(frame,0,sizeof(frame));
    menu.selection=5;
    keyboard_menu_lights(&menu,&raw,lower,upper,frame,0,true,false,false);
    for (unsigned level=1; level<=10; ++level) {
        const unsigned key=digit_sensor(level);
        const lighting_channels_t *c=&g_lighting_channels[raw.profile][key];
        if (level==5) assert(frame[c->red]==0 && frame[c->green]==255 && frame[c->blue]==0);
        else if (level<5) assert(frame[c->red]==255 && frame[c->green]==255 && frame[c->blue]==255);
        else assert(frame[c->red]==25 && frame[c->green]==25 && frame[c->blue]==25);
    }
    const lighting_channels_t *esc_c=&g_lighting_channels[raw.profile][esc];
    assert(frame[esc_c->red]==255 && frame[esc_c->green]==0 && frame[esc_c->blue]==0);
    /* Escape leaves the page. */
    values[esc]=2400; step();
    assert(!menu.velocity_page);
    values[esc]=3900; step(); drain(); logged=0;
    /* Level 10 always transmits full velocity; intermediate levels raise the floor. */
    midi.velocity_start=10; assert(strike_velocity(0x14)==127);
    midi.velocity_start=5;  assert(strike_velocity(0x14)==69); /* 56 + round(71*0.17778) */
    midi.velocity_start=2;  assert(strike_velocity(0x1e)==34); /* 14 + round(113*0.17778) = 14+20 */
    midi.velocity_start=1;  assert(strike_velocity(0x1e)==23);
    assert(strike_velocity(0x21)==23); /* the setting is global, not per key */
    midi.velocity_start=10; assert(strike_velocity(0x21)==127);
    midi.velocity_start=1;
    puts("PASS velocity start: Fn+V page, ten-step selection, bar lights, floor mapping, page exit");
}

static void midi_trigger_page(void)
{
    init(); toggle();
    const unsigned fn=fn_sensor(), tab=sensor(0x2b,0), esc=sensor(0x29,0);
    assert(raw.press[0]==3500 && raw.release[0]==3600);
    /* Fn+Tab opens the raw trigger page in MIDI mode. */
    values[fn]=values[tab]=2400; step();
    assert(menu.pending==MENU_TRIGGER && !menu.press_page);
    values[fn]=values[tab]=3900; step();
    /* 3500 sits nearest the top of the new range (level 10). */
    assert(menu.press_page && menu.selection==10 && !raw.armed);
    for (unsigned i=0;i<65;++i) values[i]=3900;
    step();
    /* Levels walk the press threshold from the default down to the
     * bottom-out floor; release thresholds are untouched. */
    assert(keyboard_raw_press_level(1)==RAW_BOTTOM_OUT);
    assert(keyboard_raw_press_level(10)==RAW_DEFAULT_RELEASE-1u);
    unsigned previous=RAW_BOTTOM_OUT;
    for (unsigned level=1; level<=10; ++level) {
        const unsigned key=digit_sensor(level);
        values[key]=500; step(); /* a digit must read as pressed at any level */
        assert(menu.press_page && menu.selection==level);
        for (unsigned i=0;i<raw.count;++i) {
            assert(raw.press[i]==keyboard_raw_press_level(level));
            assert(raw.release[i]==3600); /* the second threshold never moves */
        }
        for (unsigned i=raw.count;i<65;++i) assert(raw.press[i]==3500); /* unused slots */
        assert(raw.press[0]>=previous);
        previous=raw.press[0];
        values[key]=3900; step();
    }
    assert(raw.press[0]==RAW_DEFAULT_RELEASE-1u);
    /* The bar lights the selection and Escape leaves the page. */
    uint8_t frame[LIGHTING_FRAME_SIZE];
    memset(frame,0,sizeof(frame));
    menu.selection=4;
    keyboard_menu_lights(&menu,&raw,lower,upper,frame,0,true,false,false);
    for (unsigned level=1; level<=10; ++level) {
        const unsigned key=digit_sensor(level);
        const lighting_channels_t *c=&g_lighting_channels[raw.profile][key];
        if (level==4) assert(frame[c->red]==0 && frame[c->green]==255 && frame[c->blue]==0);
        else if (level<4) assert(frame[c->red]==255 && frame[c->green]==255 && frame[c->blue]==255);
        else assert(frame[c->red]==25 && frame[c->green]==25 && frame[c->blue]==25);
    }
    values[esc]=500; step(); /* a deep threshold needs a firm press to exit too */
    assert(!menu.press_page);
    values[esc]=3900; step(); drain(); logged=0;
    /* A deeper trigger point delays the note, and velocity still completes:
     * level 1 triggers at the floor and keeps the single follow-up. */
    (void)keyboard_raw_set_press_all(&raw,keyboard_raw_press_level(1));
    for (unsigned i=0;i<65;++i) values[i]=3900;
    step(); drain(); logged=0;
    const unsigned q=sensor(0x14,0);
    values[q]=1400; step();      /* below the deep threshold: note fires */
    values[q]=1200; step();      /* the only follow-up is below the floor */
    drain();
    assert(events(0x90,74)==1);  /* Q = D5, with a one-interval fit */
    assert(midi.errors==0);
    values[q]=3900; step(); drain(); logged=0;
    (void)keyboard_raw_set_press_all(&raw,RAW_DEFAULT_PRESS);
    puts("PASS MIDI trigger page: Fn+Tab level range, release preserved, bar, Escape, deep-trigger velocity");
}

static void janko_mode(void)
{
    init(); toggle(); /* the layout only exists in MIDI mode */
    /* Configured mapping while the mode is off: Q plays D5 (74). */
    janko_strike(0x14,74,0);
    janko_toggle(); assert(midi.janko);
    /* Staggered whole-tone rows from the specified Jankó mapping. */
    janko_strike(0x29,58,0); /* Esc  A#3 */
    janko_strike(0x1e,60,0); /* 1    C4  */
    janko_strike(0x2e,82,0); /* =    A#5 */
    janko_strike(0x2b,59,0); /* Tab  B3  */
    janko_strike(0x14,61,0); /* Q    C#4 */
    janko_strike(0x1c,71,0); /* Y    B4  */
    janko_strike(0x30,83,0); /* ]    B5  */
    janko_strike(0x39,60,0); /* Caps C4  */
    janko_strike(0x0d,74,0); /* J    D5  */
    janko_strike(0x34,82,0); /* '    A#5 */
    janko_strike(0xe1,61,2); /* LSh  C#4 */
    janko_strike(0x1d,63,0); /* Z    D#4 */
    janko_strike(0x05,71,0); /* B    B4  */
    janko_strike(0xe5,83,32);/* RSh  B5  */
    /* The Jankó layout paints its black keys (accidentals) yellow, and leaves
     * the diatonic keys to the normal note/travel backlighting. */
    {
        uint8_t frame[LIGHTING_FRAME_SIZE];
        memset(frame,0,sizeof(frame));
        keyboard_midi_lights(&midi,frame,midi.changed_at);
        const struct { unsigned usage, modifier; bool black; } keys[] = {
            {0xe1,2,true},   /* Left Shift  C#4 */
            {0x1d,0,true},   /* Z           D#4 */
            {0x07,0,true},   /* D           F#4 */
            {0x1b,0,false},  /* X           F4  */
            {0x04,0,false},  /* A           D4  */
            {0xe5,32,false}, /* Right Shift B5  */
        };
        for (unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i) {
            const unsigned index=sensor((uint8_t)(keys[i].modifier?0:keys[i].usage),(uint8_t)keys[i].modifier);
            const lighting_channels_t *c=&g_lighting_channels[midi.profile-1][index];
            const bool yellow=frame[c->red]==255 && frame[c->green]==255 && frame[c->blue]==0;
            assert(yellow==keys[i].black);
        }
    }
    /* Backspace, backslash and Enter complete their rows' whole-tone runs. */
    janko_strike(0x2a,84,0); /* BkS  C6  */
    janko_strike(0x31,85,0); /* \    C#6 */
    janko_strike(0x28,84,0); /* Ent  C6  */
    /* Fn+Left Shift is ineffective: the lower row stays enabled. */
    keyboard_midi_toggle_lower(&midi,&raw);
    assert(midi.lower_muted);
    for (unsigned i=0;i<65;++i) values[i]=3900;
    step(); drain(); logged=0; /* flush the cleanup burst; the mute stays on */
    janko_strike(0x1d,63,0); /* Z is a lower-row key and still plays D#4 */
    keyboard_midi_toggle_lower(&midi,&raw);
    for (unsigned i=0;i<65;++i) values[i]=3900;
    step(); drain(); logged=0;
    janko_strike(0x1d,63,0);
    /* Leaving the mode restores the configured mapping. */
    janko_toggle(); assert(!midi.janko);
    janko_strike(0x14,74,0);
    /* The Fn hint for J is white normally and green while the layout is active. */
    uint8_t frame[LIGHTING_FRAME_SIZE];
    const unsigned fn=fn_sensor(), j=sensor(0x0d,0);
    const lighting_channels_t *c=&g_lighting_channels[raw.profile][j];
    values[fn]=2400; step();
    memset(frame,0,sizeof(frame));
    keyboard_menu_lights(&menu,&raw,lower,upper,frame,0,true,false,false);
    assert(frame[c->red]==255 && frame[c->green]==255 && frame[c->blue]==255);
    midi.janko=true;
    memset(frame,0,sizeof(frame));
    keyboard_menu_lights(&menu,&raw,lower,upper,frame,0,true,false,true);
    assert(frame[c->red]==255 && frame[c->green]==255 && frame[c->blue]==0);
    midi.janko=false;
    values[fn]=3900; step(); drain();
    puts("PASS Jankó mode: Fn+J toggle, staggered mapping, lower-row override, configured mapping restored");
}

static void polyphony(void)
{
    for (unsigned profile=1;profile<=3;++profile) {
        init(); raw.profile=profile; raw.count=profile==3 ? 65 : 60+profile;
        step(); drain(); toggle();
        unsigned voices=0;
        bool play[65]={0};
        for (unsigned i=0;i<raw.count;++i) {
            play[i]=midi.role[i]==0;
            if (play[i]) { midi.mapping[i]=i; values[i]=3400; ++voices; }
        }
        step(); /* trigger: every window starts above the bottom-out threshold */
        for(unsigned j=1;j<=4;++j) {
            for(unsigned i=0;i<raw.count;++i) if(play[i]) values[i]=3400-j*(100+i);
            step();
        }
        for(unsigned i=0;i<raw.count;++i) if(play[i]) values[i]=1400; /* bottom-out closes each five-sample window */
        step();
        drain();
        unsigned ons=0;
        for(unsigned i=0;i<logged;++i) if(log_events[i][1]==0x90) {
            const unsigned note=log_events[i][2];
            assert(play[note] && log_events[i][3]==(unsigned)((100+note)*8000.0f/4500000.0f*127.0f+0.5f));
            ++ons;
        }
        assert(ons==voices);
        for(unsigned i=0;i<raw.count;++i) values[i]=3900;
        step(); drain();
        for(unsigned i=0;i<raw.count;++i) if(play[i]) assert(events(0x80,i)==1);
        assert(!midi.errors);
    }
}
static void octave_lights(void)
{
    for(unsigned profile=1;profile<=3;++profile) {
        init(); raw.profile=profile; raw.count=profile==3 ? 65 : 60+profile;
        step(); drain();
        midi.mode=1;
        const unsigned down=sensor(0,64), up=sensor(0,16);
        uint8_t rgb[LIGHTING_FRAME_SIZE];
        for(int shift=-10;shift<=10;++shift) {
            midi.octave=shift;
            const unsigned magnitude=shift<0 ? -shift : shift;
            const unsigned half=60*(11-magnitude);
            for(unsigned phase=0;phase<2;++phase) {
                memset(rgb,7,sizeof(rgb));
                keyboard_midi_lights(&midi,rgb,half*phase);
                for(unsigned i=0;i<raw.count;++i) {
                    const lighting_channels_t *c=&g_lighting_channels[profile-1][i];
                    const unsigned offset=c->controller*192u;
                    if(i==(shift<0 ? down : up) && shift) {
                        assert(rgb[offset+c->red]==0 && rgb[offset+c->green]==0);
                        assert(rgb[offset+c->blue]==(phase ? 0 : 255));
                    } else if(midi.role[i]>=3) {
                        assert(rgb[offset+c->red]==0 && rgb[offset+c->green]==0 && rgb[offset+c->blue]==255);
                    } else if(midi.role[i]!=2) { /* Enter retains mode marker */
                        int note=(int)midi.mapping[i]+12*shift;
                        unsigned v=midi.mapping[i]==MIDI_UNMAPPED || note<0 || note>127 ? 0 : 7;
                        assert(rgb[offset+c->red]==v && rgb[offset+c->green]==v && rgb[offset+c->blue]==v);
                    }
                }
            }
        }
        midi.mode=0; midi.octave=-3;
        memset(rgb,7,sizeof(rgb)); keyboard_midi_lights(&midi,rgb,0);
        const lighting_channels_t *c=&g_lighting_channels[profile-1][down];
        assert(rgb[c->controller*192u+c->red]==7); /* keyboard mode: no octave overlay */
        midi.octave=0;
        for(unsigned mode=0;mode<2;++mode) for(unsigned level=0;level<20;++level) {
            midi.mode=mode; menu.brightness=level;
            memset(rgb,255,sizeof(rgb));
            keyboard_midi_lights(&midi,rgb,0);
            keyboard_menu_lights(&menu,&raw,lower,upper,rgb,0,mode,false,false);
            unsigned pwm=keyboard_menu_brightness(&menu);
            for(unsigned i=0;i<raw.count;++i) {
                c=&g_lighting_channels[profile-1][i];
                unsigned offset=c->controller*192u;
                if(midi.role[i]==2 || (mode && midi.role[i]>=3)) {
                    assert(rgb[offset+c->red]==0);
                    assert(rgb[offset+c->green]==(mode?0:pwm));
                    assert(rgb[offset+c->blue]==(mode?pwm:0));
                } else if(i==sensor(0x2b,0)) {
                    assert(rgb[offset+c->red]==pwm && rgb[offset+c->green]==pwm && rgb[offset+c->blue]==pwm);
                }
            }
        }
    }
    puts("PASS octave LEDs: both signs/all magnitudes/all layouts, zero/keyboard inactive");
}

static void inverse_lighting(void)
{
    for(unsigned profile=1;profile<=3;++profile) {
        init(); raw.profile=profile; raw.count=profile==3?65:60+profile; step();
        uint8_t rgb[LIGHTING_FRAME_SIZE];
        for(unsigned sample=0;sample<=4097;++sample) {
            for(unsigned i=0;i<raw.count;++i) values[i]=sample;
            lighting_travel_frame(profile,values,lower,upper,true,rgb);
            unsigned expected=sample && sample<=4096 ? 255-lighting_travel_pwm(sample,1000,3900) : 0;
            for(unsigned i=0;i<raw.count;++i) {
                const lighting_channels_t *c=&g_lighting_channels[profile-1][i];
                unsigned off=c->controller*192;
                assert(rgb[off+c->red]==expected && rgb[off+c->green]==expected && rgb[off+c->blue]==expected);
            }
        }
        for(unsigned i=0;i<raw.count;++i) values[i]=3900;
        /* Mask follows live GUI mappings, in either direction. */
        const unsigned a=sensor(4,0), space=sensor(0x2c,0);
        for(unsigned mode=0;mode<2;++mode) for(unsigned mapped=0;mapped<2;++mapped) {
            midi.mode=mode;
            midi.mapping[a]=mapped?60:MIDI_UNMAPPED;
            midi.mapping[space]=mapped?61:MIDI_UNMAPPED;
            lighting_travel_frame(profile,values,lower,upper,true,rgb);
            keyboard_midi_lights(&midi,rgb,0);
            for(unsigned i=0;i<raw.count;++i) {
                if(midi.role[i]==2 || (mode && midi.role[i]>=3)) continue; /* control indicators */
                const lighting_channels_t *c=&g_lighting_channels[profile-1][i];
                unsigned off=c->controller*192;
                unsigned v=mode && midi.mapping[i]==MIDI_UNMAPPED?0:255;
                assert(rgb[off+c->red]==v && rgb[off+c->green]==v && rgb[off+c->blue]==v);
            }
        }
        lower[a]=0;
        lighting_travel_frame(profile,values,lower,upper,true,rgb);
        const lighting_channels_t *c=&g_lighting_channels[profile-1][a];
        assert(!rgb[c->controller*192+c->red]);
        lighting_travel_frame(profile,values,lower,upper,false,rgb);
        for(unsigned i=0;i<sizeof(rgb);++i) assert(!rgb[i]);
    }
    puts("PASS inverse lighting: all layouts/ADC values, invalid fail-dark, keyboard all-key and live MIDI mapping masks");
}

static void text_frame(const keyboard_text_t *text, const char *word, int highlight, uint32_t now)
{
    uint8_t rgb[LIGHTING_FRAME_SIZE], expected[LIGHTING_FRAME_SIZE]={0};
    memset(rgb,9,sizeof(rgb));
    assert(keyboard_text_render(text,rgb,now));
    for(unsigned pass=0;pass<2;++pass) {
        for(unsigned j=0;word[j];++j) {
            if(pass && (int)j!=highlight) continue;
            unsigned usage=word[j]=='+'?0x2e:word[j]=='-'?0x2d:word[j]=='?'?0x38:word[j]-'A'+4;
            const lighting_channels_t *c=&g_lighting_channels[raw.profile-1][sensor(usage,0)];
            unsigned offset=c->controller*192u;
            unsigned pwm=pass ? 255 : 77;
            expected[offset+c->red]=(text->color[0]*pwm+127)/255;
            expected[offset+c->green]=(text->color[1]*pwm+127)/255;
            expected[offset+c->blue]=(text->color[2]*pwm+127)/255;
        }
    }
    assert(!memcmp(rgb,expected,sizeof(rgb)));
}

static void text_display(void)
{
    for(unsigned profile=1;profile<=3;++profile) {
        init(); raw.profile=profile; raw.count=profile==3 ? 65 : 60+profile;
        step(); drain();
        keyboard_text_t text={0};
        const uint32_t start=UINT32_MAX-99; /* the first letter spans timer wrap */
        char word[]="mIdi!";
        keyboard_text_start(&text,profile,word,start);
        memset(word,'X',sizeof(word)); /* start owns its input */
        assert(text.length==4);
        for(unsigned t=0;t<3000;++t) {
            unsigned phase=t%1300;
            text_frame(&text,"MIDI",phase<800 ? (int)(phase/200) : -1,start+t);
        }
        keyboard_text_start(&text,profile,"KEYBOARD",100);
        for(unsigned t=0;t<4500;++t) {
            unsigned phase=t%2100;
            text_frame(&text,"KEYBOARD",phase<1600 ? (int)(phase/200) : -1,100+t);
        }
        for (unsigned sign=0;sign<2;++sign) {
            const char *label=sign?"LIGHT+":"LIGHT-";
            keyboard_text_start(&text,profile,label,0);
            assert(text.length==6);
            for (unsigned t=0;t<1800;++t) {
                unsigned phase=t%1700;
                text_frame(&text,label,phase<1200?(int)(phase/200):-1,t);
            }
        }
        uint8_t rgb[LIGHTING_FRAME_SIZE]; memset(rgb,9,sizeof(rgb));
        keyboard_text_stop(&text);
        assert(!keyboard_text_render(&text,rgb,101));
        for(unsigned i=0;i<sizeof(rgb);++i) assert(rgb[i]==9);
        keyboard_text_start(&text,profile,"",0); assert(!text.length);
        keyboard_text_start(&text,profile," 123!",0); assert(!text.length);
        keyboard_text_start(&text,profile,"RESET?",0);
        for (unsigned t=0; t<3400; ++t) {
            unsigned phase=t%1700;
            text_frame(&text,"RESET?",phase<1200?(int)(phase/200):-1,t);
        }
        keyboard_text_start(&text,profile,NULL,0); assert(!text.length);
        keyboard_text_start(&text,0,"MIDI",0); assert(!text.length);
        keyboard_text_start(&text,4,"MIDI",0); assert(!text.length);
        keyboard_text_start(&text,profile,"ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ",0);
        assert(text.length==KEYBOARD_TEXT_MAX);

        const unsigned fn=fn_sensor(), ent=sensor(0x28,0);
        const unsigned cancel_at[]={0,199,200,799,800,1299,1600,2099,3000,3000};
        for(unsigned release=0;release<sizeof(cancel_at)/sizeof(cancel_at[0]);++release) {
            unsigned mode=midi.mode;
            values[fn]=values[ent]=2400; step();
            assert(midi.mode==mode && menu.text.length);
            uint32_t began=menu.text.started_at, changes=midi.changes;
            assert(menu.text.color[1]==(mode?255:0) && menu.text.color[2]==(mode?0:255));
            text_frame(&menu.text,mode ? "KEYBOARD" : "MIDI",0,began);
            /* Held in hysteresis, despite down[] clearing on mode switch. */
            values[fn]=raw.release[fn]; values[ent]=raw.release[ent];
            frames+=8*cancel_at[release]; step();
            assert(menu.text.length && menu.text.started_at==began && midi.changes==changes);
            unsigned lifted=(release&1u) ? fn : ent;
            values[lifted]=raw.release[lifted]+1; step();
            assert(!menu.text.length && midi.changes==changes+1 && midi.mode==(mode^1) && !raw.armed);
            values[lifted]=2400; step(); /* repressing one key cannot restart */
            assert(!menu.text.length && midi.changes==changes+1);
            values[fn]=values[ent]=3900; step(); drain();
            assert(raw.armed);
        }
        values[fn]=values[ent]=2400; step(); assert(menu.text.length);
        keyboard_raw_frame(&raw,values,raw.count,profile,false);
        keyboard_config_t before=raw.engine.config;
        keyboard_menu_frame(&menu,&raw,lower,upper,&before,frames/8,false,midi.lower_muted,&midi.music,midi.velocity_start);
        assert(!menu.text.length);
        values[fn]=values[ent]=3900; step(); drain();
        values[fn]=values[ent]=2400; step(); assert(menu.text.length);
        keyboard_menu_cancel(&menu); assert(!menu.text.length);
    }
    puts("PASS text display: exact phases/repeats/all layouts, duplicate letters, wrap, cancellation, Schmitt-held mode chord");
}
static void wheels(void)
{
    for (unsigned profile=1; profile<=3; ++profile) {
        init(); raw.profile=profile; raw.count=profile==3?65:60+profile;
        step(); drain(); toggle();
        unsigned down=sensor(0,1), up=sensor(0,4), mod=sensor(0,8);
        const unsigned controls[]={down,up,mod,sensor(0,64),sensor(0,16)};
        for (unsigned i=0; i<5; ++i) {
            assert(midi.mapping[controls[i]]==MIDI_UNMAPPED);
            assert(!keyboard_midi_map(&midi,&raw,controls[i],60));
        }
        for (unsigned value=1; value<=4096; ++value) {
            unsigned depth=value>=3800?0:value<=1000?2800:3800-value;
            values[mod]=values[up]=value; step();
            assert(midi.modulation==(depth*127+1400)/2800);
            assert(midi.bend==8192+(depth*8191+1400)/2800);
            values[down]=value; step(); assert(midi.bend==8192); /* exact cancellation */
            values[up]=3900; step(); assert(midi.bend==8192-(depth*8192+1400)/2800);
            values[down]=3900;
        }
        assert(!midi.octave && !midi.count && !midi.errors);
        values[down]=values[up]=values[mod]=3800; step(); frames+=8; drain(); logged=0;
        /* Wheels work above the configurable Schmitt trigger, without notes. */
        values[mod]=values[up]=3700; step(); assert(!raw.down[mod] && midi.modulation==5 && midi.bend>8192);
        frames+=8; drain(); assert(events(0xb0,1)==1);
        values[mod]=values[up]=1000; step(); frames+=8; drain();
        assert(midi.sent_modulation==127 && midi.sent_bend==16383);
        assert(log_events[logged-1][1]==0xe0 && log_events[logged-1][2]==127 && log_events[logged-1][3]==127);
        values[up]=3800; values[down]=1000; step(); frames+=8; drain();
        assert(midi.sent_bend==0);
        assert(log_events[logged-1][1]==0xe0 && !log_events[logged-1][2] && !log_events[logged-1][3]);
        /* Backpressure keeps latest wheel values, never fills the note FIFO. */
        blocked=true; logged=0;
        values[down]=3800;
        for (unsigned i=0; i<100; ++i) {
            values[mod]=values[up]=1000+i*20; step(); frames+=8; drain();
        }
        assert(!logged && !midi.count && !midi.errors);
        blocked=false; frames+=8; drain();
        assert(logged==2 && midi.sent_modulation==midi.modulation && midi.sent_bend==midi.bend);
        values[mod]=values[up]=3800; step(); frames+=8; drain();
        assert(midi.sent_modulation==0 && midi.sent_bend==8192);
        for (unsigned i=0; i<logged; ++i) assert(log_events[i][1]!=0x90);
        values[mod]=values[up]=1000; step(); frames+=8; drain(); logged=0;
        keyboard_raw_invalidate(&raw); keyboard_midi_guard(&midi,&raw);
        blocked=true; drain(); assert(midi.panic==MIDI_CLEANUP_EVENTS);
        keyboard_midi_abort(&midi); assert(midi.panic==MIDI_CLEANUP_EVENTS);
        blocked=false; drain();
        assert(logged==MIDI_CLEANUP_EVENTS && log_events[131][1]==0xb0 && log_events[131][2]==1 && !log_events[131][3]);
        assert(log_events[132][1]==0xe0 && log_events[132][2]==0 && log_events[132][3]==64);
        assert(log_events[0][1]==0xb0 && log_events[0][2]==64 && !log_events[0][3]);
        assert(midi.sent_modulation==0 && midi.sent_bend==8192);
    }
    init(); const unsigned modifiers[]={1,4,8,16,64};
    for (unsigned i=0; i<5; ++i) {
        unsigned at=sensor(0,modifiers[i]); values[at]=1000; step(); drain();
        assert(raw.engine.report.modifiers==(modifiers[i]>=16?0:modifiers[i]) && !logged);
        if(modifiers[i]>=16) assert(keyboard_report_get_usage(&raw.engine.report,modifiers[i]==16?0x4f:0x50));
        values[at]=3900; step();
    }
    puts("PASS wheels: all ADC values/layouts, exact cancellation/endpoints, independent Schmitt, packets/latest-only backpressure, cleanup and keyboard-mode controls");
}
static void menu_input_isolation(void)
{
    for (unsigned mode=0; mode<2; ++mode) {
        init(); if (mode) toggle();
        values[menu.fn]=2400; step();
        for (unsigned i=0; i<4; ++i) {
            press_fit(i&1 ? menu.l : menu.k);
            values[menu.k]=values[menu.l]=3900; step(); drain();
            assert(menu.brightness_session && !raw.armed);
        }
        values[menu.fn]=3900; step(); drain();
        values[menu.fn]=values[menu.r]=2400; step();
        values[menu.fn]=values[menu.r]=3900; step(); step();
        assert(menu.reset_confirmation && menu.confirmation_ready);
        /* Unrelated notes/typing and N itself must not escape confirmation. */
        press_fit(sensor(0x04,0)); drain();
        assert(menu.reset_confirmation && !raw.armed);
        assert(!keyboard_report_get_usage(&raw.engine.report,0x04));
        press_fit(menu.n); drain(); assert(!menu.reset_confirmation && !raw.armed);
        for (unsigned i=0; i<logged; ++i) assert(log_events[i][1]!=0x90 && log_events[i][1]!=0xa0);
        for (unsigned i=0; i<raw.count; ++i) values[i]=3900;
        step(); drain(); assert(raw.armed);
        press_fit(sensor(0x04,0)); drain();
        if (mode) assert(events(0x90,61)==1);
        else assert(keyboard_report_get_usage(&raw.engine.report,0x04));
    }
}
static void lower_toggle(unsigned release_fn_first)
{
    const bool old=midi.lower_muted;
    values[menu.fn]=values[menu.shift]=2400; step();
    assert(menu.pending==MENU_LOWER && midi.lower_muted==old);
    assert(menu.text.length==(old?8u:9u)); /* LOWER-ON / LOWER-OFF */
    const char *word=old?"LOWER-ON":"LOWER-OFF";
    for(unsigned t=0;t<menu.text.length*200u+500u;++t)
        text_frame(&menu.text,word,t<menu.text.length*200u?(int)(t/200u):-1,menu.text.started_at+t);
    for (unsigned i=0;i<20;++i) step();
    assert(midi.lower_muted==old);
    values[release_fn_first?menu.fn:menu.shift]=3900; step();
    assert(midi.lower_muted!=old && !raw.armed);
    for (unsigned i=0;i<20;++i) step();
    assert(midi.lower_muted!=old); /* no repeated action while held */
    values[menu.fn]=values[menu.shift]=3900; step(); drain(); step(); logged=0;
}

static bool physical_lower(unsigned profile,unsigned sensor_index)
{
    for(unsigned p=0;p<KEYBOARD_GRID_SIZE;++p) {
        const keyboard_grid_cell_t *c=&g_keyboard_grid[p];
        unsigned index=profile==1?c->ansi:profile==2?c->iso:c->jis;
        if(index==sensor_index) return (p>=27 && p<=44) || (p>=57 && p<=61) || (p>=63 && p<=67);
    }
    assert(false); return false;
}

static void lower_rows(void)
{
    for(unsigned profile=1;profile<=3;++profile) {
        init(); raw.profile=profile; raw.count=profile==3?65:60+profile;
        keyboard_raw_invalidate(&raw); step(); drain();
        /* Fn+Left Shift must not become a settings action in keyboard mode. */
        values[menu.fn]=values[menu.shift]=2400; step(); assert(!menu.pending && !midi.lower_muted);
        values[menu.fn]=values[menu.shift]=3900; step();
        toggle();
        values[menu.shift]=2400; step(); values[menu.fn]=2400; step();
        assert(!menu.pending); /* preheld S cannot become a settings press */
        values[menu.shift]=3900; step(); values[menu.shift]=2400; step();
        assert(menu.pending==MENU_LOWER);
        keyboard_raw_enable(&raw,false); step(); assert(!menu.pending && !midi.lower_muted);
        values[menu.fn]=values[menu.shift]=3900;
        keyboard_raw_enable(&raw,true); step(); drain();
        /* Remap every assignable sensor, including Enter and unassigned extras.
         * Filtering must depend on physical rows, never note number/defaults. */
        for(unsigned i=0;i<raw.count;++i) {
            (void)keyboard_midi_map(&midi,&raw,i,20+i); step(); drain();
        }
        uint8_t saved[65]; memcpy(saved,midi.mapping,sizeof(saved));
        lower_toggle(profile%2); assert(midi.lower_muted);
        for(unsigned i=0;i<raw.count;++i) {
            bool lower_row=physical_lower(profile,i);
            assert(!!(midi.lower_rows[i/8] & (1u<<(i%8)))==lower_row);
            if(midi.mapping[i]==255) continue;
            logged=0; press_fit(i); drain();
            assert(events(0x90,20+i)==!lower_row);
            frames+=80; step(); drain();
            if(lower_row) assert(!events(0xa0,20+i));
            values[i]=3900; step(); drain();
            assert(events(0x80,20+i)==!lower_row);
        }
        uint8_t lights[LIGHTING_FRAME_SIZE]; memset(lights,255,sizeof(lights));
        keyboard_midi_lights(&midi,lights,0);
        for(unsigned i=0;i<raw.count;++i) if(physical_lower(profile,i)) {
            const lighting_channels_t *ch=&g_lighting_channels[profile-1][i];
            uint8_t *p=lights+ch->controller*192u;
            assert(!p[ch->red] && !p[ch->green] && p[ch->blue]==(i==menu.enter?255:0));
        }
        const unsigned ctrl=sensor(0,1),alt=sensor(0,4),win=sensor(0,8),right=sensor(0,16);
        values[ctrl]=1000; values[win]=1000; values[right]=1000; step();
        assert(midi.bend==0 && midi.modulation==127 && midi.octave==1);
        values[ctrl]=3900; values[alt]=1000; step(); assert(midi.bend==16383);
        for(unsigned i=0;i<raw.count;++i) values[i]=3900;
        step(); drain(); midi.octave=0;
        toggle(); assert(!midi.mode && midi.lower_muted);
        press_fit(menu.shift); assert(raw.engine.report.modifiers==2);
        values[menu.shift]=3900; step(); toggle(); assert(midi.lower_muted);
        lower_toggle(!(profile%2)); assert(!midi.lower_muted && !memcmp(saved,midi.mapping,sizeof(saved)));
        press_fit(menu.shift); drain(); assert(events(0x90,20+menu.shift)==1);
        /* Preview cancels a sounding lower note and an upper pending strike,
         * even when USB is backpressured. Cleanup must finish before replay. */
        values[sensor(0x2b,0)]=2400; step();
        blocked=true; values[menu.fn]=2400; values[menu.shift]=3900; step();
        values[menu.shift]=2400; step(); assert(menu.pending==MENU_LOWER && midi.panic);
        values[menu.shift]=values[menu.fn]=3900; step();
        assert(midi.lower_muted && midi.panic && !midi.count);
        for(unsigned i=0;i<128;++i) assert(!midi.refs[i]);
        for(unsigned i=0;i<raw.count;++i) values[i]=3900;
        blocked=false; drain(); step(); logged=0;
        for(unsigned i=0;i<8;++i) step();
        drain();
        assert(!events(0x90,20+sensor(0x2b,0)) && !events(0x90,20+menu.shift));
        keyboard_midi_init(&midi); assert(!midi.lower_muted);
    }
    puts("PASS lower rows: Fn+Left Shift release toggle, all layouts/custom maps, dark notes, mode persistence, wheels, pending/shared cleanup and startup defaults");
}

static void music_data(void)
{
    const unsigned intervals[][12]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10},
        {0,2,3,5,7,9,10},{0,1,3,5,7,8,10},{0,2,4,6,7,9,11},
        {0,2,4,5,7,9,10},{0,1,3,5,6,8,10},{0,2,4,7,9},{0,3,5,7,10},
        {0,1,2,3,4,5,6,7,8,9,10,11}};
    const unsigned counts[]={7,7,7,7,7,7,7,5,5,12};
    const char *selectors="JIDHYMLPOT";
    for(unsigned scale=0;scale<MIDI_SCALE_COUNT;++scale) {
        assert(midi_scales[scale].selector==selectors[scale]);
        assert(midi_music_scale_selector(selectors[scale]-'A'+4)==(int)scale);
        for(unsigned root=0;root<12;++root) for(unsigned note=0;note<128;++note) {
            bool expected=false;
            for(unsigned i=0;i<counts[scale];++i) expected|=(note+12-root)%12==intervals[scale][i];
            assert(midi_music_contains(&(midi_music_config_t){root,scale},note)==expected);
        }
    }
    assert(!midi_music_contains(NULL,60));
    assert(!midi_music_contains(&(midi_music_config_t){12,0},60));
    assert(!midi_music_contains(&(midi_music_config_t){0,MIDI_SCALE_COUNT},60));
    assert(!midi_music_contains(&(midi_music_config_t){0,0},128));
    for(unsigned i=0;i<MIDI_ROOT_KEY_COUNT;++i) assert(midi_music_root_selector(midi_root_keys[i].usage)==(int)(i%12));
    assert(midi_music_root_selector(0x29)==-1 && midi_music_scale_selector(0x04)==-1);
}

static void open_music(unsigned page)
{
    unsigned at=page==MENU_KEY?menu.e:menu.s;
    values[menu.fn]=values[at]=2400; step();
    assert(menu.pending==page && !menu.music_page);
    values[menu.fn]=values[at]=3900; step();
    assert(menu.music_page==page && !menu.choice_ready);
    step(); step(); drain(); logged=0;
    assert(menu.choice_ready && !raw.armed);
}

static void select_music(unsigned usage, unsigned page, unsigned result)
{
    const unsigned at=sensor(usage,0);
    const midi_music_config_t old=midi.music;
    open_music(page);
    values[at]=2400; step();
    assert(menu.music_page==page && menu.choice_sensor==at && menu.selection==result);
    const char *word=page==MENU_KEY?midi_root_names[result]:midi_scales[result].name;
    for(unsigned t=0;t<menu.text.length*200u+500u;t+=200)
        text_frame(&menu.text,word,t<menu.text.length*200u?(int)(t/200u):-1,menu.text.started_at+t);
    for(unsigned i=0;i<8;++i) step();
    assert(!memcmp(&old,&midi.music,sizeof(old)) && !logged);
    values[at]=raw.release[at]; step(); assert(menu.music_page==page);
    values[at]++; step(); assert(!menu.music_page);
    assert(page==MENU_KEY?midi.music.root==result:midi.music.scale==result);
    values[at]=3900; step(); drain(); logged=0;
}

static void music_menus(void)
{
    const uint8_t root_usage[]={0x2b,0x1e,0x14,0x1f,0x1a,0x08,0x21,0x15,0x22,0x17,0x23,0x1c};
    for(unsigned profile=1;profile<=3;++profile) {
        init(); raw.profile=profile; raw.count=profile==3?65:60+profile;
        keyboard_raw_invalidate(&raw); step(); drain(); toggle();
        uint8_t saved[65]; memcpy(saved,midi.mapping,sizeof(saved));
        assert(!midi.music.root && midi.music.scale==MIDI_SCALE_CHROMATIC);
        for(unsigned scale=0;scale<MIDI_SCALE_COUNT;++scale)
            select_music(midi_scales[scale].selector-'A'+4,MENU_SCALE,scale);
        for(unsigned root=0;root<12;++root) select_music(root_usage[root],MENU_KEY,root);
        assert(!memcmp(saved,midi.mapping,sizeof(saved)));
        /* Escape cancels even while a choice is being previewed. */
        open_music(MENU_SCALE); values[sensor('J'-'A'+4,0)]=2400; step();
        values[sensor(0x29,0)]=2400; step();
        assert(!menu.music_page && midi.music.scale==MIDI_SCALE_CHROMATIC);
        for(unsigned i=0;i<raw.count;++i) values[i]=3900;
        step(); drain();
        /* Two simultaneous selectors are rejected until everything is up. */
        open_music(MENU_SCALE);
        unsigned j=sensor('J'-'A'+4,0),i=sensor('I'-'A'+4,0);
        values[j]=values[i]=2400; step(); assert(!menu.choice_ready && menu.choice_sensor==255);
        values[j]=3900; step(); assert(!menu.choice_ready);
        values[i]=3900; step(); step(); assert(menu.choice_ready);
        keyboard_menu_cancel(&menu); step(); drain();
        for(unsigned fault=0;fault<5;++fault) {
            open_music(MENU_KEY); values[sensor(0x2b,0)]=2400; step();
            if(fault==0) keyboard_raw_enable(&raw,false);
            if(fault==1) ++raw.revision;
            if(fault==2) {
                keyboard_config_t before=raw.engine.config;
                keyboard_menu_frame(&menu,&raw,lower,upper,&before,frames/8,true,midi.lower_muted,&midi.music,midi.velocity_start);
            }
            if(fault==3) keyboard_menu_cancel(&menu);
            if(fault==4) values[0]=0;
            step(); assert(!menu.music_page && midi.music.root==11);
            for(unsigned k=0;k<raw.count;++k) values[k]=3900;
            keyboard_raw_enable(&raw,true); step(); drain();
        }
        assert(keyboard_midi_select_music(&midi,&raw,2,MIDI_SCALE_DORIAN)); step(); drain();
        toggle(); assert(midi.music.root==2 && midi.music.scale==MIDI_SCALE_DORIAN);
        press_fit(menu.e); assert(keyboard_report_get_usage(&raw.engine.report,0x08));
        values[menu.e]=3900; step(); toggle();
        assert(midi.music.root==2 && midi.music.scale==MIDI_SCALE_DORIAN);
        keyboard_midi_init(&midi); assert(!midi.music.root && midi.music.scale==MIDI_SCALE_CHROMATIC);
    }
    puts("PASS root/scale menus: all roots/scales/layouts, exact choice words, held/release semantics, cancellation, chord rejection, mappings and persistence");
}

static void music_output(void)
{
    init(); toggle();
    for(unsigned root=0;root<12;++root) for(unsigned scale=0;scale<MIDI_SCALE_COUNT;++scale) {
        assert(keyboard_midi_select_music(&midi,&raw,root,scale)); step(); drain(); logged=0;
        bool expected[128]={false};
        for(unsigned i=0;i<raw.count;++i) if(midi.mapping[i]!=255) {
            values[i]=2400;
            if(midi_music_contains(&midi.music,midi.mapping[i])) expected[midi.mapping[i]]=true;
        }
        step();
        for(unsigned frame=0;frame<5;++frame) {
            for(unsigned i=0;i<raw.count;++i) if(midi.mapping[i]!=255) values[i]-=100;
            step();
        }
        for(unsigned i=0;i<raw.count;++i) if(midi.mapping[i]!=255) values[i]=1400; /* bottom-out closes each window */
        step();
        drain();
        for(unsigned note=0;note<128;++note) assert(events(0x90,note)==expected[note]);
        uint8_t rgb[LIGHTING_FRAME_SIZE]; memset(rgb,255,sizeof(rgb));
        keyboard_midi_lights(&midi,rgb,0);
        for(unsigned i=0;i<raw.count;++i) if(midi.role[i]==0) {
            const lighting_channels_t *c=&g_lighting_channels[0][i];
            unsigned v=midi.mapping[i]!=255 && expected[midi.mapping[i]]?255:0;
            assert(rgb[c->red]==v && rgb[c->green]==v && rgb[c->blue]==v);
        }
        for(unsigned i=0;i<raw.count;++i) values[i]=3900;
        step(); drain();
        for(unsigned note=0;note<128;++note) assert(events(0x80,note)==expected[note]);
    }
    /* GUI assignments are filtered by pitch, not physical key legend. */
    assert(keyboard_midi_map(&midi,&raw,menu.c,73)); step(); drain();
    assert(keyboard_midi_select_music(&midi,&raw,0,MIDI_SCALE_MAJOR)); step(); drain(); logged=0;
    press_fit(menu.c); drain(); assert(!events(0x90,73));
    values[menu.c]=3900; step();
    assert(keyboard_midi_select_music(&midi,&raw,1,MIDI_SCALE_MAJOR)); step(); drain(); logged=0;
    press_fit(menu.c); drain(); assert(events(0x90,73)==1);
    blocked=true;
    assert(keyboard_midi_select_music(&midi,&raw,0,MIDI_SCALE_MAJOR));
    assert(midi.panic && !raw.armed && !midi.refs[73]);
    assert(!keyboard_midi_select_music(&midi,&raw,12,0));
    assert(!keyboard_midi_select_music(&midi,&raw,0,MIDI_SCALE_COUNT));
    blocked=false; values[menu.c]=3900; step(); drain(); logged=0;
    assert(keyboard_midi_select_music(&midi,&raw,0,MIDI_SCALE_MAJOR)); step(); drain();
    keyboard_midi_toggle_lower(&midi,&raw); step(); drain(); logged=0;
    press_fit(sensor(0x10,0)); drain(); assert(!events(0x90,72)); /* M is in scale but muted */
    values[sensor(0x10,0)]=3900; step(); press_fit(sensor(0x2b,0)); drain(); assert(events(0x90,72)==1);
    puts("PASS all 120 root/scale filters: polyphonic USB events and LED masks agree; remapping, lower-row intersection and cleanup");
}

static void sustain_pedal(void)
{
    for(unsigned profile=1;profile<=3;++profile) {
        init(); raw.profile=profile; raw.count=profile==3?65:60+profile;
        keyboard_raw_invalidate(&raw); step(); drain();
        const unsigned space=sensor(0x2c,0),tab=sensor(0x2b,0);
        press_fit(space); drain(); assert(keyboard_report_get_usage(&raw.engine.report,0x2c) && !midi.sustain);
        values[space]=3900; step(); toggle();
        assert(midi.mapping[space]==255 && !keyboard_midi_map(&midi,&raw,space,60));
        assert(keyboard_midi_select_music(&midi,&raw,1,MIDI_SCALE_MAJOR)); step(); drain();
        keyboard_midi_toggle_lower(&midi,&raw); step(); drain(); logged=0;
        values[space]=3500; step(); drain(); assert(!logged && !midi.sustain);
        values[space]=3499; step(); drain(); assert(midi.sustain && events(0xb0,64)==1);
        assert(log_events[0][3]==127);
        for(unsigned j=0;j<20;++j) {values[space]=j%2?3500:3600; step(); drain();}
        assert(events(0xb0,64)==1);
        values[space]=3601; step(); drain(); assert(!midi.sustain && events(0xb0,64)==2);
        assert(log_events[logged-1][3]==0);
        uint8_t rgb[LIGHTING_FRAME_SIZE]={0}; keyboard_midi_lights(&midi,rgb,0);
        const lighting_channels_t *c=&g_lighting_channels[profile-1][space];
        unsigned offset=c->controller*192u;
        assert(rgb[offset+c->red]==0 && rgb[offset+c->green]==0 && rgb[offset+c->blue]==255);
        assert(keyboard_midi_select_music(&midi,&raw,0,MIDI_SCALE_CHROMATIC)); step(); drain();
        press_fit(tab); drain(); logged=0;
        /* Same-scan pedal down is delivered before the note release. */
        values[space]=2400; values[tab]=3900; step(); drain();
        assert(logged>=2 && log_events[0][1]==0xb0 && log_events[0][2]==64 && log_events[0][3]==127);
        assert(log_events[1][1]==0x80 && log_events[1][2]==72);
        values[space]=3900; step(); drain(); logged=0;
        blocked=true;
        for(unsigned j=0;j<8;++j) {values[space]=j%2?3900:2400; step(); drain();}
        assert(!logged && midi.count==8);
        blocked=false; drain(); assert(logged==8);
        for(unsigned j=0;j<8;++j) assert(log_events[j][1]==0xb0 && log_events[j][2]==64 && log_events[j][3]==(j%2?0:127));
        values[space]=2400; step(); drain(); logged=0;
        keyboard_raw_invalidate(&raw); keyboard_midi_guard(&midi,&raw);
        blocked=true; drain(); assert(midi.panic==MIDI_CLEANUP_EVENTS && !midi.sustain);
        blocked=false; drain(); assert(log_events[0][1]==0xb0 && log_events[0][2]==64 && !log_events[0][3]);
        /* A press swallowed during cleanup must not turn sustain on afterward. */
        values[space]=3900; step(); keyboard_midi_abort(&midi);
        values[space]=2400; step(); drain(); logged=0;
        step(); drain(); assert(!midi.sustain && !events(0xb0,64));
        values[space]=3900; step(); values[space]=2400; step(); drain();
        assert(midi.sustain && events(0xb0,64)==1);
        /* Fn cancels the pedal and cannot reassert it while Space stays held. */
        values[menu.fn]=2400; step(); drain(); assert(!midi.sustain);
        values[menu.fn]=3900; step(); drain(); assert(!midi.sustain);
        values[space]=3900; step(); logged=0; blocked=true;
        for(unsigned j=0;j<140 && !midi.errors;++j) {values[space]=j%2?3900:2400; step();}
        assert(midi.errors==1 && midi.panic && !midi.sustain && !raw.armed);
        blocked=false; drain();
    }
    puts("PASS sustain: all layouts, reserved Space/blue hint, Schmitt boundaries, ordered CC64/note release, backpressure edges, fault/overflow pedal-off, keyboard Space unaffected");
}

int main(void)
{
    default_mapping(); velocity_pressure_and_modes(); short_taps_and_overlap(); janko_mode();
    velocity_start_mode(); midi_trigger_page();
    octave_and_duplicates(); faults_and_backpressure(); polyphony(); shift_and_filtered_strike(); octave_lights();
    text_display(); inverse_lighting(); menu_input_isolation(); wheels(); lower_rows(); music_data(); music_menus(); music_output(); sustain_pedal();
    printf("MIDI tests passed; controller state %zu bytes\n",sizeof(keyboard_midi_t));
    return 0;
}
