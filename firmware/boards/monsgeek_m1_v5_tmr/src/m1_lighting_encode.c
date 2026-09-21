#include "m1_lighting.h"

bool m1_lighting_encode(const uint8_t *rgb,size_t length,uint8_t *wire,size_t capacity)
{
    if(!rgb || !wire || length!=M1_LED_BYTES || capacity<M1_LED_WIRE_BYTES)return false;
    /* Input and output must be distinct buffers. No additional brightness or
     * gamma transform: the common application has already composed RGB. */
    static const uint8_t order[]={1,0,2};
    for(unsigned led=0;led<M1_KEY_COUNT;++led)
        for(unsigned channel=0;channel<3;++channel) {
            unsigned value=rgb[3u*led+order[channel]];
            for(unsigned bit=0;bit<8;++bit)
                wire[24u*led+8u*channel+bit]=value&(0x80u>>bit)?
                    M1_LED_SYMBOL_ONE:M1_LED_SYMBOL_ZERO;
        }
    return true;
}
