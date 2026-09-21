#include "fun60_board.h"
#include "keyboard_layout.h"
#include "keyboard_raw.h"
#include "defaults.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned row, channel, clocks, conversions, fail_at;
static bool gate, data;
static void signal_pin(fun60_signal_t pin,bool high)
{
    if (pin==FUN60_DATA) data=high;
    if (pin==FUN60_CLOCK && high) { assert(gate);++clocks; }
    if (pin==FUN60_GATE && !high) {
        assert(!data);
        unsigned previous=row ? 14u-row : 0u;
        assert(clocks==previous+row+1u);
        clocks=0;
    }
    if (pin==FUN60_GATE) gate=high;
}
static void mux_pin(unsigned value) { assert(value<6);channel=value; }
static void delay(unsigned us) { assert(us>0 && us<=5); }
static bool adc(uint16_t *value)
{
    assert(!gate && channel>=1 && channel<=5);
    if (++conversions==fail_at) return false;
    *value=(uint16_t)(row*100+channel);
    if (channel==5) ++row;
    return true;
}
static void reset_io(void) { row=channel=clocks=conversions=fail_at=0;gate=true;data=false; }

int main(void)
{
    unsigned seen[61]={0};
    for (unsigned key=0;key<FUN60_KEY_COUNT;++key) {
        const fun60_key_t *k=&fun60_keys[key];
        assert(k->row<14 && k->channel>=1 && k->channel<=5);
        assert(fun60_sensor(k->row,k->channel)==(int)key);
        assert(k->led<61 && !seen[k->led]++);
        assert(k->x4+k->width4<=60 && k->y<5);
        assert(keyboard_key_for_sensor(FUN60_PROFILE,key)==key+1);
    }
    for (unsigned y=0;y<5;++y) {
        unsigned x=0;
        for (unsigned key=0;key<61;++key) if (fun60_keys[key].y==y) {
            assert(fun60_keys[key].x4==x);x+=fun60_keys[key].width4;
        }
        assert(x==60);
    }
    assert(fun60_sensor(14,1)==-1 && fun60_sensor(0,0)==-1);
    assert(!keyboard_layout(1));
    assert(keyboard_layout(FUN60_PROFILE)->sample_hz==FUN60_SCAN_HZ);
    uint8_t rgb[183],wire[FUN60_LED_BYTES];
    for (unsigned value=0;value<256;++value) {
        memset(rgb,value,sizeof(rgb));
        fun60_encode_lights(rgb,wire);
        for (unsigned bit=0;bit<sizeof(wire);++bit)
            assert(wire[bit]==((value & (0x80u>>(bit%8u))) ? 0xf0u : 0xc0u));
    }
    memset(rgb,0,sizeof(rgb));
    rgb[3*29]=0x80; /* A's red component, not green/blue */
    fun60_encode_lights(rgb,wire);
    for (unsigned bit=0;bit<sizeof(wire);++bit)
        assert(wire[bit]==(bit==24u*fun60_keys[29].led+8u ? 0xf0u : 0xc0u));
    const fun60_scan_io_t io={signal_pin,mux_pin,delay,adc};
    uint16_t frame[61];
    reset_io();
    assert(fun60_scan(&io,frame));
    assert(conversions==70 && gate && channel==0 && row==14 && clocks==14);
    for (unsigned key=0;key<61;++key)
        assert(frame[key]==fun60_keys[key].row*100+fun60_keys[key].channel);
    for (unsigned fail=1;fail<=70;++fail) {
        reset_io();fail_at=fail;
        memset(frame,0xa5,sizeof(frame));
        assert(!fun60_scan(&io,frame));
        assert(gate && channel==0 && !data);
        for (unsigned key=0;key<61;++key) assert(frame[key]==0xa5a5);
    }
    keyboard_raw_t keys;
    keyboard_raw_init(&keys);
    for (unsigned key=0;key<61;++key) frame[key]=4096;
    keyboard_raw_frame(&keys,frame,61,FUN60_PROFILE,true);
    assert(keys.armed);
    frame[29]=2000;
    keyboard_raw_frame(&keys,frame,61,FUN60_PROFILE,true);
    assert(keyboard_report_get_usage(&keys.engine.report,0x04));
    frame[29]=4096;
    keyboard_raw_frame(&keys,frame,61,FUN60_PROFILE,true);
    assert(!keyboard_report_get_usage(&keys.engine.report,0x04));
    puts("FUN60: layout, LED encoding, complete/failed scans, portable key actuation passed");
    return 0;
}
