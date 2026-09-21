#include "m1_sleep.h"
#include "m1_startup.h"
#include "m1_board.h"
#include "defaults.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"
#include "at32f402_405_debug.h"

static bool ready;
static volatile bool timer_wake;
static bool clock_flag(uint32_t flag)
{
    for(unsigned i=0;i<AT32_CLOCK_WAIT_LOOPS;++i)
        if(crm_flag_get(flag)==SET)return true;
    return false;
}
static bool context_valid(void)
{ return !__get_IPSR() && !__get_BASEPRI() && !__get_FAULTMASK() && !(__get_CONTROL()&1u); }
static bool main_clock(void)
{
    crm_clocks_freq_type c;
    crm_clocks_freq_get(&c);
    return c.sclk_freq==M1_CORE_HZ && c.ahb_freq==M1_CORE_HZ &&
           c.apb1_freq==M1_CORE_HZ/2u && c.apb2_freq==M1_CORE_HZ;
}
static bool stop_timer(void)
{
    ertc_interrupt_enable(ERTC_WAT_INT,FALSE);
    bool stopped=ertc_wakeup_enable(FALSE)==SUCCESS;
    ertc_flag_clear(ERTC_WATF_FLAG);
    exint_flag_clear(EXINT_LINE_22);
    NVIC_ClearPendingIRQ(ERTC_WKUP_IRQn);
    return stopped;
}
bool m1_sleep_ready(void) { return ready; }
bool m1_sleep_init(void)
{
    if(!context_valid() || !main_clock())return false;
    if(ready)return true;
    /* Changing an established backup clock would require a domain reset.
     * Do not erase somebody else's retained data to make sleep initialize. */
    if(CRM->bpdc_bit.ertcsel && CRM->bpdc_bit.ertcsel!=CRM_ERTC_CLOCK_LICK)return false;
    uint32_t mask=__get_PRIMASK(); __disable_irq();
    crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK,TRUE);
    pwc_battery_powered_domain_access(TRUE);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_LICK,TRUE);
    if(!clock_flag(CRM_LICK_STABLE_FLAG)) { __set_PRIMASK(mask); return false; }
    crm_ertc_clock_select(CRM_ERTC_CLOCK_LICK);
    crm_ertc_clock_enable(TRUE);
    NVIC_DisableIRQ(ERTC_WKUP_IRQn);
    if(!stop_timer() || ertc_divider_set(M1_RTC_DIV_A,M1_RTC_DIV_B)!=SUCCESS ||
       ertc_wait_update()!=SUCCESS) {
        ertc_write_protect_enable(); __set_PRIMASK(mask); return false;
    }
    ertc_wakeup_clock_set(ERTC_WAT_CLK_CK_B_16BITS);
    exint_init_type line;
    exint_default_para_init(&line);
    line.line_select=EXINT_LINE_22; line.line_mode=EXINT_LINE_INTERRUPUT;
    line.line_polarity=EXINT_TRIGGER_RISING_EDGE; line.line_enable=TRUE;
    exint_init(&line);
    NVIC_ClearPendingIRQ(ERTC_WKUP_IRQn);
    NVIC_SetPriority(ERTC_WKUP_IRQn,3);
    NVIC_EnableIRQ(ERTC_WKUP_IRQn);
    timer_wake=false; ready=true;
    __set_PRIMASK(mask);
    return true;
}
void m1_sleep_irq(void)
{
    if(!ready || ertc_interrupt_flag_get(ERTC_WATF_FLAG)==RESET)return;
    ertc_flag_clear(ERTC_WATF_FLAG);
    exint_flag_clear(EXINT_LINE_22);
    timer_wake=true;
}
m1_sleep_result_t m1_sleep_wait(uint32_t ticks,bool platform_quiescent)
{
    if(!ready || !ticks || ticks>M1_RTC_MAX_TICKS)return M1_SLEEP_NOT_READY;
    if(!context_valid())return M1_SLEEP_CONTEXT;
    if(!platform_quiescent)return M1_SLEEP_BUSY;
    uint32_t mask=__get_PRIMASK(); __disable_irq();
    /* No DMA may outlive the clocks/rails it is using. Deep-sleep debug is
     * explicitly incompatible with the SDK extra-low-power regulator mode. */
    if(DMA1_CHANNEL1->ctrl_bit.chen || DMA1_CHANNEL2->ctrl_bit.chen ||
       DMA1_CHANNEL3->ctrl_bit.chen || DMA1_CHANNEL6->ctrl_bit.chen ||
       (TMR2->ctrl1&1u) || (TMR3->ctrl1&1u) || (TMR6->ctrl1&1u) || PWC->ctrl_bit.lpsel ||
       ADC1->ctrl2_bit.adcen || spi_i2s_flag_get(SPI2,SPI_I2S_BF_FLAG)==SET ||
       spi_i2s_flag_get(SPI3,SPI_I2S_BF_FLAG)==SET ||
       (GPIOB->odt&(GPIO_PINS_6|GPIO_PINS_13)) ||
       (GPIOC->odt&(GPIO_PINS_6|GPIO_PINS_14)) ||
       (DEBUGMCU->ctrl&DEBUG_DEEPSLEEP) || !main_clock()) {
        __set_PRIMASK(mask); return M1_SLEEP_BUSY;
    }
    if(!stop_timer()) {
        ready=false; __set_PRIMASK(mask); return M1_SLEEP_RTC_ERROR;
    }
    ertc_wakeup_counter_set(ticks-1u);
    timer_wake=false;
    ertc_interrupt_enable(ERTC_WAT_INT,TRUE);
    if(ertc_wakeup_enable(TRUE)!=SUCCESS) {
        (void)stop_timer(); ready=false; __set_PRIMASK(mask); return M1_SLEEP_RTC_ERROR;
    }
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HICK,TRUE);
    if(!clock_flag(CRM_HICK_STABLE_FLAG)) {
        if(!stop_timer())ready=false;
        __set_PRIMASK(mask); return M1_SLEEP_HICK_ERROR;
    }
    uint32_t systick=SysTick->CTRL & (SysTick_CTRL_ENABLE_Msk|SysTick_CTRL_TICKINT_Msk|SysTick_CTRL_CLKSOURCE_Msk);
    SysTick->CTRL=systick&~SysTick_CTRL_ENABLE_Msk;
    crm_hick_sclk_frequency_select(CRM_HICK_SCLK_8MHZ);
    crm_hick_sclk_div_set(CRM_HICK_SCLK_DIV_1);
    crm_sysclk_switch(CRM_SCLK_HICK);
    bool hick=false;
    for(unsigned i=0;i<AT32_CLOCK_WAIT_LOOPS;++i)
        if(crm_sysclk_switch_status_get()==CRM_SCLK_HICK) { hick=true; break; }
    if(hick) {
        /* The board reference and RM specify 1.0 V here, despite the newer
         * SDK enum omitting it. Continue using the SDK's register macro. */
        pwc_ldo_output_voltage_set(M1_SLEEP_LDO_SELECTOR);
        pwc_voltage_regulate_set(PWC_REGULATOR_EXTRA_LOW_POWER);
        __DSB();
        pwc_deep_sleep_mode_enter(PWC_DEEP_SLEEP_ENTER_WFI);
        __ISB();
    }
    pwc_voltage_regulate_set(PWC_REGULATOR_ON);
    if(m1_clock_init()!=M1_CLOCK_OK) {
        /* Keep every ordinary IRQ and SysTick stopped on a failed resume. */
        (void)stop_timer(); ready=false; return M1_SLEEP_CLOCK_FATAL;
    }
    m1_sleep_irq(); /* acknowledge RTC before unmasking, clocks now restored */
    bool stopped=stop_timer();
    if(!stopped)ready=false;
    SysTick->CTRL=systick;
    __set_PRIMASK(mask);
    if(!stopped)return M1_SLEEP_RTC_ERROR;
    if(!hick)return M1_SLEEP_HICK_ERROR;
    return timer_wake?M1_SLEEP_TIMER:M1_SLEEP_OTHER_WAKE;
}
