#include "m1_time.h"
#include "m1_board.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

enum { OFF, RUNNING, SUSPENDED, FAULT };
static unsigned state;
static uint32_t last, fraction;
static m1_time_point_t time;
_Static_assert(M1_CORE_HZ%M1_TIME_HZ==0 && M1_CORE_HZ/M1_TIME_HZ<=65536u,
    "timebase must have an exact 16-bit prescaler");
static bool context(void)
{ return !__get_IPSR() && !__get_BASEPRI() && !__get_FAULTMASK() && !(__get_CONTROL()&1u); }
static bool clocks(void)
{
    /* Avoid passing a zero PLL denominator into the SDK clock calculator. */
    if(CRM->cfg_bit.sclksts==CRM_SCLK_PLL && !CRM->pllcfg_bit.pllms)return false;
    crm_clocks_freq_type c;crm_clocks_freq_get(&c);
    return c.sclk_freq==M1_CORE_HZ && c.ahb_freq==M1_CORE_HZ &&
        c.apb1_freq==M1_CORE_HZ/2u && c.apb2_freq==M1_CORE_HZ;
}
static bool retained(bool running)
{
    return clocks() && CRM->apb1en_bit.tmr2en && !CRM->apb1rst_bit.tmr2rst &&
        TMR2->ctrl1==(M1_TIME_PLUS_BIT|(running?1u:0u)) &&
        !TMR2->ctrl2 && !TMR2->stctrl && !TMR2->iden && !TMR2->cctrl &&
        TMR2->pr==UINT32_MAX && TMR2->div==M1_CORE_HZ/M1_TIME_HZ-1u &&
        !NVIC_GetEnableIRQ(TMR2_GLOBAL_IRQn);
}
static void advance(uint32_t elapsed)
{
    time.us+=elapsed;
    /* Divide the delta, not the wrapping microsecond timestamp. Carry the
     * remainder without an overflowing elapsed+fraction intermediate. */
    time.ms+=elapsed/1000u;
    fraction+=elapsed%1000u;
    time.ms+=fraction/1000u;fraction%=1000u;
}
static void sample(void)
{
    uint32_t count=tmr_counter_value_get(TMR2);
    advance(count-last);last=count;
}
bool m1_time_start(void)
{
    if(!context() || state!=OFF || !clocks())return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    if(TMR2->ctrl1_bit.tmren || TMR2->iden || TMR2->cctrl ||
       NVIC_GetEnableIRQ(TMR2_GLOBAL_IRQn) || CRM->apb1rst_bit.tmr2rst) {
        __set_PRIMASK(mask);return false;
    }
    crm_periph_clock_enable(CRM_TMR2_PERIPH_CLOCK,TRUE);
    tmr_reset(TMR2);
    NVIC_ClearPendingIRQ(TMR2_GLOBAL_IRQn);
    tmr_32_bit_function_enable(TMR2,TRUE);
    tmr_base_init(TMR2,UINT32_MAX,M1_CORE_HZ/M1_TIME_HZ-1u);
    tmr_counter_value_set(TMR2,0);
    tmr_flag_clear(TMR2,TMR_OVF_FLAG);
    time=(m1_time_point_t){0};last=fraction=0;state=RUNNING;
    tmr_counter_enable(TMR2,TRUE);
    __DMB();__set_PRIMASK(mask);return true;
}
bool m1_time_now(m1_time_point_t *out)
{
    if(!out || !context() || state!=RUNNING)return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    bool valid=retained(true);
    if(valid) { sample();*out=time; }
    else state=FAULT;
    __DMB();__set_PRIMASK(mask);return valid;
}
bool m1_time_suspend(m1_time_point_t *out)
{
    if(!out || !context() || state!=RUNNING)return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    bool valid=retained(true);
    if(valid) {
        tmr_counter_enable(TMR2,FALSE);__DSB();
        sample();*out=time;state=SUSPENDED;
    } else state=FAULT;
    __DMB();__set_PRIMASK(mask);return valid;
}
bool m1_time_resume(uint32_t elapsed_us)
{
    if(!context() || state!=SUSPENDED)return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    bool valid=retained(false) && tmr_counter_value_get(TMR2)==last;
    if(valid) {
        advance(elapsed_us);state=RUNNING;tmr_counter_enable(TMR2,TRUE);
    } else state=FAULT;
    __DMB();__set_PRIMASK(mask);return valid;
}
bool m1_time_stop(void)
{
    if(!context() || state==OFF)return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    tmr_counter_enable(TMR2,FALSE);state=OFF;
    __DSB();__set_PRIMASK(mask);return true;
}
bool m1_time_healthy(void) { return state==RUNNING || state==SUSPENDED; }
