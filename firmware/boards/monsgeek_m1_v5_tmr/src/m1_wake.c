#include "m1_wake.h"
#include "defaults.h"
#include <string.h>

void m1_wake_init(m1_wake_t *s,const bool enabled[M1_KEY_COUNT])
{
    /* Allow callers to reuse the current enable mask when beginning an episode. */
    bool mask[M1_KEY_COUNT];
    for(unsigned i=0;i<M1_KEY_COUNT;++i)mask[i]=!enabled || enabled[i];
    *s=(m1_wake_t){0};
    memcpy(s->enabled,mask,sizeof(mask));
}
m1_wake_result_t m1_wake_frame(m1_wake_t *s,const uint16_t frame[M1_KEY_COUNT],uint32_t sequence)
{
    if(s->result!=M1_WAKE_WAIT)return s->result;
    if(s->have_sequence && sequence==s->sequence)return M1_WAKE_WAIT;
    if(!frame || (s->have_sequence && (uint32_t)(sequence-s->sequence)!=1u))
        return s->result=M1_WAKE_FAULT;
    for(unsigned i=0;i<M1_KEY_COUNT;++i)
        if(!frame[i] || frame[i]>M1_ADC_MAX+1u)return s->result=M1_WAKE_FAULT;
    s->sequence=sequence; s->have_sequence=true;
    memcpy(s->frame,frame,sizeof(s->frame));
    if(s->acquired<M1_WAKE_ACQUIRE_FRAMES) {
        ++s->acquired;
        for(unsigned i=0;i<M1_KEY_COUNT;++i)
            if(s->enabled[i])s->baseline[i]=frame[i];
        return M1_WAKE_WAIT; /* last baseline-acquisition frame cannot wake */
    }
    for(unsigned i=0;i<M1_KEY_COUNT;++i) {
        if(!s->enabled[i])continue;
        if(frame[i]==s->baseline[i])s->drift[i]=0;
        else if(++s->drift[i]>=M1_WAKE_REFRESH_FRAMES) {
            s->drift[i]=0; s->baseline[i]=frame[i];
        }
        /* Refresh precedes this strict comparison in the original policy.
         * Canonical +1 normalization preserves the native count difference. */
        if((uint32_t)frame[i]+M1_WAKE_DROP_COUNTS<s->baseline[i]) {
            s->baseline[i]=frame[i];
            s->triggered[i]=true; s->result=M1_WAKE_KEYS;
        }
    }
    return s->result;
}
bool m1_wake_snapshot(const m1_wake_t *s,uint16_t frame[M1_KEY_COUNT],bool triggered[M1_KEY_COUNT])
{
    if(!s || s->result!=M1_WAKE_KEYS || !frame || !triggered)return false;
    memcpy(frame,s->frame,sizeof(s->frame));
    memcpy(triggered,s->triggered,sizeof(s->triggered));
    return true;
}
