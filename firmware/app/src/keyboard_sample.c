#include "keyboard_sample.h"
bool keyboard_sample_normalize(uint16_t native, uint16_t released_full_scale,
                               uint16_t pressed_full_scale, uint16_t *canonical)
{
    if(!canonical || released_full_scale==pressed_full_scale) return false;
    uint32_t travel,span;
    if(released_full_scale>pressed_full_scale) {
        span=released_full_scale-pressed_full_scale;
        travel=native>=released_full_scale ? 0u : native<=pressed_full_scale ? span :
            (uint32_t)released_full_scale-native;
    } else {
        span=pressed_full_scale-released_full_scale;
        travel=native<=released_full_scale ? 0u : native>=pressed_full_scale ? span :
            (uint32_t)native-released_full_scale;
    }
    *canonical=4096u-(travel*4095u+span/2u)/span;
    return true;
}

bool keyboard_samples_travel(const uint16_t *native, const uint16_t *lower,
                             const uint16_t *upper, unsigned count,
                             unsigned minimum_span, uint16_t *canonical)
{
    if(!native || !lower || !upper || !canonical || !count ||
       !minimum_span || minimum_span>=4096u)return false;
    for(unsigned i=0;i<count;++i) {
        unsigned sample=native[i],lo=lower[i],hi=upper[i];
        if(!sample || sample>4096u || !lo || hi>4096u || hi<lo+minimum_span)return false;
        /* Endpoint clipping needs no division. Interior points retain the
         * scalar normalizer's exact integer rounding, not an approximation. */
        if(sample>=hi)canonical[i]=4096u;
        else if(sample<=lo)canonical[i]=1u;
        else {
            unsigned span=hi-lo;
            canonical[i]=4096u-((hi-sample)*4095u+span/2u)/span;
        }
    }
    return true;
}
