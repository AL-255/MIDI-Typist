#include "m1_factory.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

static void read_record(uintptr_t address,m1_factory_record_t *out)
{
    const volatile uint8_t *page=(const volatile uint8_t *)address;
    for(unsigned i=0;i<sizeof(out->values);++i)out->values[i]=page[i];
    for(unsigned i=0;i<sizeof(out->trailer);++i)
        out->trailer[i]=page[M1_FACTORY_TRAILER_OFFSET+i];
}
m1_factory_result_t m1_factory_load(m1_factory_bounds_t *out)
{
    if(!out)return M1_FACTORY_ARGUMENT;
    if(__get_IPSR() || __get_BASEPRI() || __get_FAULTMASK() || (__get_CONTROL()&1u))
        return M1_FACTORY_CONTEXT;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    if(FLASH->sts_bit.obf) { __set_PRIMASK(mask);return M1_FACTORY_BUSY; }
    m1_factory_record_t upper,lower;
    read_record(M1_FACTORY_UPPER_ADDRESS,&upper);
    read_record(M1_FACTORY_LOWER_ADDRESS,&lower);
    __set_PRIMASK(mask);
    return m1_factory_decode(&upper,&lower,out);
}
