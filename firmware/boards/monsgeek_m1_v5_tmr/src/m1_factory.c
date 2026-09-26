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
        /* Saved bounds must be in the electrical ADC domain. Never infer a
         * scale from an out-of-range record or rewrite its factory page. */
        if(hi<M1_FACTORY_RELEASE_MIN_RAW || hi>M1_FACTORY_RELEASE_MAX_RAW ||
           lo>M1_ADC_MAX || !keyboard_sample_normalize(hi,M1_ADC_MAX,0,&staged.upper[i]) ||
           !keyboard_sample_normalize(lo,M1_ADC_MAX,0,&staged.lower[i]))return M1_FACTORY_RANGE;
    }
    if(!calibration_bounds_valid(M1_PROFILE,M1_KEY_COUNT,staged.lower,staged.upper))
        return M1_FACTORY_RANGE;
    *out=staged;return M1_FACTORY_OK;
}
bool m1_factory_bootstrap(const uint16_t released[M1_KEY_COUNT],m1_factory_bounds_t *out)
{
    if(!released || !out)return false;
    m1_factory_bounds_t staged;
    for(unsigned i=0;i<M1_KEY_COUNT;++i) {
        /* Acquisition is already ADC+1. Reject rails/invalid input rather
         * than allowing the reference's unsigned floor subtraction to wrap. */
        if(released[i]<M1_FACTORY_RELEASE_MIN_RAW+1u ||
           released[i]>M1_FACTORY_RELEASE_MAX_RAW+1u)return false;
        staged.upper[i]=released[i];
        staged.lower[i]=released[i]-M1_STARTUP_TRAVEL_RAW;
    }
    *out=staged;return true;
}
