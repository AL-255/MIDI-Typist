#include "keyboard_encoder.h"
#include <stddef.h>

_Static_assert(ENCODER_PHASE_STABLE_SAMPLES>0 && ENCODER_PHASE_STABLE_SAMPLES<=255,
               "encoder phase debounce range");
_Static_assert(ENCODER_BUTTON_DEBOUNCE_MS>0,"encoder button debounce range");
bool keyboard_encoder_init(keyboard_encoder_t *s,uint8_t phase,bool pressed,uint32_t hz)
{
    uint64_t ticks=((uint64_t)hz*ENCODER_BUTTON_DEBOUNCE_MS+999u)/1000u;
    if(!s || phase>3 || !hz || !ticks || ticks>UINT16_MAX)return false;
    *s=(keyboard_encoder_t){.phase=phase,.candidate=phase,.button=pressed,
        .button_armed=!pressed,.button_samples=(uint16_t)ticks};
    return true;
}
uint8_t keyboard_encoder_sample(keyboard_encoder_t *s,uint8_t phase,bool pressed)
{
    if(!s || !s->button_samples || phase>3)return 0;
    uint8_t events=0;
    if(phase==s->phase) { s->candidate=phase;s->phase_count=0; }
    else {
        if(phase!=s->candidate) { s->candidate=phase;s->phase_count=0; }
        if(++s->phase_count>=ENCODER_PHASE_STABLE_SAMPLES) {
            /* Gray-code transitions. A two-bit jump cannot establish direction;
             * rebaseline instead of guessing or carrying a partial turn. */
            static const int8_t delta[16]={0,1,-1,0,-1,0,0,1,1,0,0,-1,0,-1,1,0};
            if((phase^s->phase)==3) { s->movement=0;++s->invalid_transitions; }
            else s->movement+=delta[4*s->phase+phase];
            s->phase=phase;s->phase_count=0;
            if(s->movement==4) { events|=ENCODER_POSITIVE;s->movement=0; }
            if(s->movement==-4) { events|=ENCODER_NEGATIVE;s->movement=0; }
        }
    }
    if(pressed==s->button)s->button_count=0;
    else if(++s->button_count>=s->button_samples) {
        s->button_count=0;s->button=pressed;
        if(s->button_armed)events|=pressed?ENCODER_PRESS:ENCODER_RELEASE;
        if(!pressed)s->button_armed=true;
    }
    return events;
}
