#include "keyboard_calibration.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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

    calibration_finish(&c,true,70000); assert(c.state==CAL_DONE);
    puts("PASS calibration timing, all layouts, time wrap, parallel holds, noise, cancel and invalid samples");
}
