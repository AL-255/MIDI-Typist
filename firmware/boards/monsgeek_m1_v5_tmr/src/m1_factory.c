#include "m1_factory.h"
#include "keyboard_calibration.h"
#include "keyboard_sample.h"
#include "defaults.h"

static uint16_t halfword(const uint8_t *p)
{ return (uint16_t)p[0] | (uint16_t)p[1]<<8; }
m1_factory_result_t m1_factory_decode(const m1_factory_record_t *upper,
    const m1_factory_record_t *lower,m1_factory_bounds_t *out)
{
    if(!upper || !lower || !out)return M1_FACTORY_ARGUMENT;
    if(upper->trailer[1]!=0x55 || upper->trailer[2]!=0xaa ||
       lower->trailer[1]!=0x55 || lower->trailer[2]!=0xaa)return M1_FACTORY_MARKER;
    if(upper->trailer[0]!=1 || lower->trailer[0]!=1)return M1_FACTORY_UNCALIBRATED;
    m1_factory_bounds_t staged;
    for(unsigned i=0;i<M1_KEY_COUNT;++i) {
        unsigned cell=m1_factory_cell(i);
        if(cell>=M1_FACTORY_CELL_COUNT)return M1_FACTORY_RANGE;
        uint16_t hi=halfword(upper->values+2u*cell),lo=halfword(lower->values+2u*cell);
        /* Reference baseline validity is 1000..4000. The custom application
         * additionally rejects reversed/narrow/wrapped pairs, never applying
         * the reference's sample-minus-700 fallback to unknown travel. */
        if(hi<M1_FACTORY_RELEASE_MIN_RAW || hi>M1_FACTORY_RELEASE_MAX_RAW ||
           lo>M1_ADC_MAX || !keyboard_sample_normalize(hi,M1_ADC_MAX,0,&staged.upper[i]) ||
           !keyboard_sample_normalize(lo,M1_ADC_MAX,0,&staged.lower[i]))return M1_FACTORY_RANGE;
    }
    if(!calibration_bounds_valid(M1_PROFILE,M1_KEY_COUNT,staged.lower,staged.upper))
        return M1_FACTORY_RANGE;
    *out=staged;return M1_FACTORY_OK;
}
