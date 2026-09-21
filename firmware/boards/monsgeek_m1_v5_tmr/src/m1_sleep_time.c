#include "m1_sleep_time.h"
#include "m1_time.h"
#include "defaults.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

enum { OFF, MEASURING, READY, FAULT };
static unsigned state;
static uint32_t start_tick,start_us,measured_ticks,measured_us,remainder;
static bool context(void)
{ return !__get_IPSR() && !__get_BASEPRI() && !__get_FAULTMASK() && !(__get_CONTROL()&1u); }
static bool stamp(uint32_t *out)
{
    if(!m1_sleep_ready() || CRM->bpdc_bit.ertcsel!=CRM_ERTC_CLOCK_LICK ||
       !CRM->bpdc_bit.ertcen || !CRM->ctrlsts_bit.lickstbl ||
       ERTC->div_bit.diva!=M1_RTC_DIV_A || ERTC->div_bit.divb!=M1_RTC_DIV_B ||
       ERTC->ctrl_bit.dren || ERTC->ctrl_bit.hm || ERTC->ctrl_bit.rcden ||
       ERTC->ctrl_bit.add1h || ERTC->ctrl_bit.dec1h ||
       ERTC->sts_bit.imen || ERTC->sts_bit.tadjf || !ERTC->sts_bit.updf)return false;
    /* RM 18.3.2: SBS locks TIME/DATE until DATE is read. SDK calendar_get
     * performs that final unlock; sub_second_get alone would unlock too soon. */
    uint32_t sub=ERTC->sbs,raw=ERTC->time;
    ertc_time_type calendar;ertc_calendar_get(&calendar);
    if(sub>M1_RTC_DIV_B || calendar.hour>23 || calendar.min>59 || calendar.sec>59 ||
       (raw&15u)>9 || ((raw>>8)&15u)>9 || ((raw>>16)&15u)>9 ||
       (raw&~0x003f7f7fu))return false;
    *out=((uint32_t)calendar.hour*3600u+calendar.min*60u+calendar.sec)*
        (M1_RTC_DIV_B+1u)+M1_RTC_DIV_B-sub;
    return true;
}
static uint32_t difference(uint32_t after,uint32_t before)
{ return after>=before?after-before:M1_RTC_DAY_TICKS-before+after; }
bool m1_sleep_time_begin(void)
{
    if(!context() || state!=OFF)return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    m1_time_point_t time;
    bool valid=stamp(&start_tick) && m1_time_now(&time);
    if(valid) { start_us=time.us;state=MEASURING;remainder=0; }
    else state=FAULT;
    __set_PRIMASK(mask);return valid;
}
void m1_sleep_time_service(void)
{
    if(state!=MEASURING || !context())return;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    uint32_t tick;m1_time_point_t time;
    if(!stamp(&tick) || !m1_time_now(&time))state=FAULT;
    else {
        uint32_t us=time.us-start_us,elapsed=difference(tick,start_tick);
        if(us>M1_SLEEP_CLOCK_TIMEOUT_US)state=FAULT;
        else if(us>=M1_SLEEP_CLOCK_WINDOW_US) {
            uint64_t cycles=(uint64_t)elapsed*(M1_RTC_DIV_A+1u)*1000000u;
            uint64_t slop=(uint64_t)M1_SLEEP_CLOCK_SLOP_TICKS*(M1_RTC_DIV_A+1u)*1000000u;
            if(!elapsed || cycles+slop<(uint64_t)us*M1_LICK_MIN_HZ ||
               cycles>(uint64_t)us*M1_LICK_MAX_HZ+slop)state=FAULT;
            else { measured_us=us;measured_ticks=elapsed;state=READY; }
        }
    }
    __set_PRIMASK(mask);
}
bool m1_sleep_time_ready(void) { return state==READY; }
bool m1_sleep_time_fault(void) { return state==FAULT; }
m1_sleep_result_t m1_sleep_timed_wait(uint32_t ticks,bool permission)
{
    if(!context())return M1_SLEEP_CONTEXT;
    if(state!=READY || !ticks || ticks>M1_RTC_MAX_TICKS)return M1_SLEEP_NOT_READY;
    if(!permission)return M1_SLEEP_BUSY;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    uint32_t before,after;m1_time_point_t time;
    if(!stamp(&before) || !m1_time_suspend(&time)) {
        state=FAULT;__set_PRIMASK(mask);return M1_SLEEP_TIME_ERROR;
    }
    m1_sleep_result_t result=m1_sleep_wait(ticks,true);
    if(result==M1_SLEEP_CLOCK_FATAL) { state=FAULT;return result; }
    /* Deepsleep stops shadow updates. The bounded official SDK sync is
     * required even if a non-RTC interrupt caused the early wake. */
    ertc_write_protect_disable();
    bool valid=ertc_wait_update()==SUCCESS;
    ertc_write_protect_enable();
    valid=valid && stamp(&after);
    if(valid) {
        uint32_t elapsed=difference(after,before);
        uint64_t margin=((uint64_t)M1_SLEEP_CLOCK_RESUME_MARGIN_US*measured_ticks+
                         measured_us-1u)/measured_us;
        /* The timer must wake within its requested tick count; allow bounded
         * clock/SDK restoration time, not an apparent whole-day rollback. */
        valid=elapsed<=(uint64_t)(ticks+1u)*(M1_RTC_DIV_B+1u)+margin;
        uint64_t scaled=(uint64_t)elapsed*measured_us+remainder;
        uint64_t us=scaled/measured_ticks;
        valid=valid && us<=UINT32_MAX && m1_time_resume((uint32_t)us);
        if(valid)remainder=scaled%measured_ticks;
    }
    if(!valid) { state=FAULT;result=M1_SLEEP_TIME_ERROR; }
    __set_PRIMASK(mask);return result;
}
