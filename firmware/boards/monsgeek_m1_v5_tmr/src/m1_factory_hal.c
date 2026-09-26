#include "m1_factory.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"
#include <string.h>

static void read_record(uintptr_t address,m1_factory_record_t *out)
{
    const volatile uint8_t *page=(const volatile uint8_t *)address;
    for(unsigned i=0;i<sizeof(out->values);++i)out->values[i]=page[i];
    for(unsigned i=0;i<sizeof(out->trailer);++i)
        out->trailer[i]=page[M1_FACTORY_TRAILER_OFFSET+i];
}
m1_factory_result_t m1_factory_read(m1_factory_record_t *upper,m1_factory_record_t *lower)
{
    if(!upper || !lower)return M1_FACTORY_ARGUMENT;
    if(__get_IPSR() || __get_BASEPRI() || __get_FAULTMASK() || (__get_CONTROL()&1u))
        return M1_FACTORY_CONTEXT;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    if(FLASH->sts_bit.obf) { __set_PRIMASK(mask);return M1_FACTORY_BUSY; }
    read_record(M1_FACTORY_UPPER_ADDRESS,upper);
    read_record(M1_FACTORY_LOWER_ADDRESS,lower);
    __set_PRIMASK(mask);
    return M1_FACTORY_OK;
}
m1_factory_result_t m1_factory_load(m1_factory_bounds_t *out)
{
    if(!out)return M1_FACTORY_ARGUMENT;
    m1_factory_record_t upper,lower;
    m1_factory_result_t result=m1_factory_read(&upper,&lower);
    if(result!=M1_FACTORY_OK)return result;
    return m1_factory_decode(&upper,&lower,out);
}
m1_factory_result_t m1_factory_read_dump(uint8_t out[M1_FACTORY_DUMP_BYTES])
{
    if(!out)return M1_FACTORY_ARGUMENT;
    m1_factory_record_t upper,lower;
    m1_factory_result_t result=m1_factory_read(&upper,&lower);
    if(result!=M1_FACTORY_OK)return result;
    const uint8_t header[]={'M','1','F','C',1,0,M1_FACTORY_CELL_COUNT&255u,M1_FACTORY_CELL_COUNT>>8};
    _Static_assert(sizeof(m1_factory_record_t)==M1_FACTORY_VALUES_BYTES+3u,"packed calibration fields");
    memcpy(out,header,sizeof(header));
    memcpy(out+8,&upper,sizeof(upper));memcpy(out+8+sizeof(upper),&lower,sizeof(lower));
    return M1_FACTORY_OK;
}
