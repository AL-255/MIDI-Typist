#include "keyboard_encoder.h"
#include "keyboard_aux.h"
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
static uint16_t sent[32];
static unsigned sent_count;
static bool accept;
static bool send_consumer(uint16_t usage)
{ if(!accept)return false;assert(sent_count<32);sent[sent_count++]=usage;return true; }
static void auxiliary(void)
{
    keyboard_aux_t s;const uint16_t mapping[]={0xe9,0xea,0xe2};
    keyboard_aux_init(&s,mapping);accept=false;
    assert(!keyboard_aux_offer(&s,1) && !keyboard_aux_idle(&s));
    keyboard_aux_service(&s,true,0,send_consumer);assert(!keyboard_aux_idle(&s));
    accept=true;keyboard_aux_service(&s,true,0,send_consumer);
    assert(keyboard_aux_idle(&s) && sent_count==1 && !sent[0]);
    assert(keyboard_aux_offer(&s,5)); /* rotation and button share a sample */
    keyboard_aux_service(&s,true,UINT32_MAX-5,send_consumer);
    assert(sent[1]==0xe9 && !keyboard_aux_offer(&s,2));
    accept=false;keyboard_aux_service(&s,true,AUX_PULSE_MS,send_consumer);
    assert(sent_count==2 && s.usage==0xe9);
    accept=true;keyboard_aux_service(&s,true,AUX_PULSE_MS,send_consumer);
    assert(sent[2]==0 && !keyboard_aux_idle(&s));
    keyboard_aux_service(&s,true,AUX_PULSE_MS+1,send_consumer);assert(sent[3]==0xe2);
    keyboard_aux_cancel(&s);assert(!keyboard_aux_idle(&s));
    keyboard_aux_service(&s,false,AUX_PULSE_MS+2,send_consumer);
    assert(sent[4]==0 && keyboard_aux_idle(&s) && !keyboard_aux_offer(&s,1));
    keyboard_aux_service(&s,true,100,send_consumer);assert(keyboard_aux_offer(&s,2));
    keyboard_aux_service(&s,false,101,send_consumer);assert(sent_count==6 && !sent[5]);
    keyboard_aux_service(&s,true,102,send_consumer);
    assert(keyboard_aux_offer(&s,8)); /* release does not toggle mute twice */
    keyboard_aux_service(&s,true,103,send_consumer);assert(sent_count==6);
    assert(keyboard_aux_idle(&s) && !keyboard_aux_offer(&s,16));
}
int main(void)
{
    auxiliary();
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
