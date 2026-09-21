/* Offline fixture: encode real application objects through the production
 * encoder. stdout contains one binary record for the Python GUI decoder. */
#include "keyboard_telemetry.h"
#include "keyboard_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv)
{
    assert(argc==3);
    unsigned profile=(unsigned)strtoul(argv[1],NULL,10);
    unsigned scenario=(unsigned)strtoul(argv[2],NULL,10);
    assert(profile<256 && scenario<4);
    keyboard_app_t app;
    keyboard_raw_t raw;
    keyboard_midi_t midi;
    keyboard_menu_t menu;
    keyboard_calibration_t cal;
    keyboard_app_init(&app,&raw,&midi,&menu,&cal,NULL);
    device_store_t store={.slot=255};
    keyboard_telemetry_t status={.calibration_supported=true};
    uint8_t guarded[KEYBOARD_TELEMETRY_SIZE+2];
    memset(guarded,0xa5,sizeof(guarded));
    uint8_t *out=guarded+1; /* intentionally unaligned wire buffer */
    assert(!keyboard_telemetry_encode(out,NULL,&store,&status,0));
    assert(!keyboard_telemetry_encode(out,&app,&store,NULL,0));
    raw.profile=254;
    assert(!keyboard_telemetry_encode(out,&app,&store,&status,0));
    raw.profile=profile;raw.count=KEYBOARD_TELEMETRY_KEYS+1;
    assert(!keyboard_telemetry_encode(out,&app,&store,&status,0));
    raw.profile=raw.count=0;
    cal.count=KEYBOARD_TELEMETRY_KEYS+1;
    assert(!keyboard_telemetry_encode(out,&app,&store,&status,0));
    cal.count=0;
    for(unsigned i=0;i<sizeof(guarded);++i) assert(guarded[i]==0xa5);

    if(scenario) {
        unsigned count=keyboard_layout_count(profile);
        assert(count && count<=KEYBOARD_TELEMETRY_KEYS);
        uint16_t samples[MT_KEY_CAPACITY],lo[MT_KEY_CAPACITY],hi[MT_KEY_CAPACITY];
        for(unsigned i=0;i<count;++i) { samples[i]=4000;lo[i]=1000;hi[i]=4000; }
        keyboard_app_frame(&app,samples,count,profile,lo,hi,true,0);
        raw.armed=false;raw.revision=0x10203040;
        raw.engine.config.mode=2;raw.engine.config.fn=0; /* wire Fn comes from physical state */
        cal.state=CAL_COLLECT;cal.count=count;cal.profile=profile;
        cal.selected=count-1;cal.completed=1;cal.done[0]=1;
        cal.holds[count-1].active=true;
        cal.holds[count-1].since=UINT32_MAX-20u;
        cal.activity=UINT32_MAX-10u;
        cal.upper[count-1]=4000;cal.lower[count-1]=1000;
        for(unsigned i=0;i<count;++i) {
            raw.raw[i]=3000+i;raw.press[i]=2100+i;raw.release[i]=3700+i;
            raw.down[i]=i==count-1 || keyboard_key_for_sensor(profile,i)==keyboard_layout(profile)->fn;
            raw.velocity[i]=(keyboard_velocity_t){
                .value=(float)(i%5)/4.0f,.captures=1234+i,.ready=true,.valid=i%2,.pending=i%2
            };
        }
        midi.mode=1;midi.octave=-3;midi.janko=true;midi.panic=2;
        midi.errors=0x12345678;midi.changes=0x87654321;
        keyboard_report_set_usage(&app.sent,0x04,true);
        keyboard_report_set_usage(&app.sent,0xe0,true);
        store=(device_store_t){.saved=true,.valid=true,.pending=true,.fault=true,.slot=1,
            .generation=0x1234abcd,.calibration_generation=0xfedcba98,.error=73};
        status.sequence=0xffffffff;status.ack=0x11223344;status.result=2;
        status.scan_fault=status.light_fault=true;
        status.scan_errors=42;status.light_errors=43;
        if(scenario==2) {
            cal.holds[count-1].since=20u-CALIBRATION_HOLD_MS-50u;
            cal.activity=20u-CALIBRATION_IDLE_MS-1u;
        }
        if(scenario==3) status.calibration_supported=false;
    }
    assert(keyboard_telemetry_encode(out,&app,scenario==3 ? NULL : &store,&status,20));
    assert(guarded[0]==0xa5 && guarded[sizeof(guarded)-1]==0xa5);
    assert(fwrite(out,1,KEYBOARD_TELEMETRY_SIZE,stdout)==KEYBOARD_TELEMETRY_SIZE);
    return 0;
}
