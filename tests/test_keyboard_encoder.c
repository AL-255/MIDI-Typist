#include "keyboard_encoder.h"
#include <assert.h>
#include <stdio.h>

static uint8_t stable(keyboard_encoder_t *s,unsigned phase,bool button,unsigned ticks)
{
    uint8_t events=0;
    while(ticks--)events|=keyboard_encoder_sample(s,phase,button);
    return events;
}
static uint8_t cycle(keyboard_encoder_t *s,bool positive,bool button)
{
    const uint8_t forward[]={1,3,2,0},reverse[]={2,3,1,0};uint8_t events=0;
    for(unsigned i=0;i<4;++i)events|=stable(s,(positive?forward:reverse)[i],button,
                                         ENCODER_PHASE_STABLE_SAMPLES);
    return events;
}
int main(void)
{
    keyboard_encoder_t s;
    assert(!keyboard_encoder_init(NULL,0,false,8000));
    assert(!keyboard_encoder_init(&s,4,false,8000));
    assert(!keyboard_encoder_init(&s,0,false,0));
    assert(!keyboard_encoder_init(&s,0,false,UINT32_MAX));
    assert(keyboard_encoder_init(&s,0,false,8000));
    for(unsigned i=0;i<500;++i) {
        assert(cycle(&s,true,false)==ENCODER_POSITIVE);
        assert(cycle(&s,false,false)==ENCODER_NEGATIVE);
    }
    assert(!s.invalid_transitions);
    /* Returning along the same path is not a turn. */
    for(unsigned i=0;i<100;++i) {
        assert(!stable(&s,1,false,ENCODER_PHASE_STABLE_SAMPLES));
        assert(!stable(&s,0,false,ENCODER_PHASE_STABLE_SAMPLES));
    }
    /* Different candidates must not accumulate into a stable transition. */
    if(ENCODER_PHASE_STABLE_SAMPLES>1) {
        for(unsigned i=0;i<50;++i) {
            assert(!keyboard_encoder_sample(&s,1,false));
            assert(!keyboard_encoder_sample(&s,2,false));
        }
        assert(s.phase==0 && !s.movement);
    }
    assert(!stable(&s,3,false,ENCODER_PHASE_STABLE_SAMPLES));
    assert(s.invalid_transitions==1 && !s.movement);
    assert(!stable(&s,0,false,ENCODER_PHASE_STABLE_SAMPLES));
    assert(s.invalid_transitions==2 && !s.movement);
    assert(cycle(&s,true,false)==ENCODER_POSITIVE);
    unsigned n=s.button_samples;
    assert(!stable(&s,0,true,n-1));
    assert(!stable(&s,0,false,1));
    assert(!stable(&s,0,true,n-1));
    assert(stable(&s,0,true,1)==ENCODER_PRESS);
    assert(!stable(&s,0,true,3*n));
    assert(stable(&s,0,false,n)==ENCODER_RELEASE);
    assert(keyboard_encoder_init(&s,0,true,8000));
    assert(!stable(&s,0,true,n*2));
    assert(!stable(&s,0,false,n)); /* held-at-start release never escapes */
    assert(stable(&s,0,true,n)==ENCODER_PRESS);
    assert(stable(&s,0,false,n)==ENCODER_RELEASE);
    /* Sampling rate controls time debounce; no board/SDK enters shared code. */
    assert(keyboard_encoder_init(&s,3,false,1000));
    assert(s.button_samples==ENCODER_BUTTON_DEBOUNCE_MS);
    assert(!keyboard_encoder_sample(&s,4,false));
    puts("PASS shared encoder: directions, bounce, invalid transitions, neutral restart, button debounce");
}
