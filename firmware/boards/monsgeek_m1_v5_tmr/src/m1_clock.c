#include "m1_startup.h"
#include "m1_board.h"
#include "defaults.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

static bool wait_flag(uint32_t flag,flag_status value)
{
    for(unsigned i=0;i<AT32_CLOCK_WAIT_LOOPS;++i)
        if(crm_flag_get(flag)==value)return true;
    return false;
}
static bool wait_source(crm_sclk_type source)
{
    for(unsigned i=0;i<AT32_CLOCK_WAIT_LOOPS;++i)
        if(crm_sysclk_switch_status_get()==source)return true;
    return false;
}
static m1_clock_result_t failed(m1_clock_result_t why)
{
    crm_auto_step_mode_enable(FALSE);
    return why;
}
m1_clock_result_t m1_clock_init(void)
{
    if(!__get_PRIMASK())return M1_CLOCK_INTERRUPTS;
    /* Raise wait states before changing frequency; this is the PSR latency
     * register, not an erase/program command. Never lower it on a failed boot. */
    flash_psr_set(FLASH_WAIT_CYCLE_6);
    crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK,TRUE);
    crm_auto_step_mode_enable(TRUE);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HICK,TRUE);
    if(!wait_flag(CRM_HICK_STABLE_FLAG,SET))return failed(M1_CLOCK_HICK);
    crm_sysclk_switch(CRM_SCLK_HICK);
    if(!wait_source(CRM_SCLK_HICK))return failed(M1_CLOCK_HICK_SWITCH);
    /* RM 3.5: change the regulator only while running from HICK/HEXT. */
    pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);
    /* Only reprogram the PLL after proving execution moved off it. The SDK's
     * crm_reset/SystemInit contain unbounded waits and are not called here. */
    crm_pllu_output_set(FALSE);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL,FALSE);
    if(!wait_flag(CRM_PLL_STABLE_FLAG,RESET))return failed(M1_CLOCK_PLL_STOP);
    crm_hick_sclk_frequency_select(CRM_HICK_SCLK_8MHZ);
    crm_hick_sclk_div_set(CRM_HICK_SCLK_DIV_1);
    crm_ahb_div_set(CRM_AHB_DIV_1);
    crm_apb1_div_set(CRM_APB1_DIV_2);
    crm_apb2_div_set(CRM_APB2_DIV_1);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT,FALSE);
    if(!wait_flag(CRM_HEXT_STABLE_FLAG,RESET))return failed(M1_CLOCK_HEXT_STOP);
    crm_hext_bypass(FALSE);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT,TRUE);
    if(!wait_flag(CRM_HEXT_STABLE_FLAG,SET))return failed(M1_CLOCK_HEXT);
    /* Board oscillator/PLL wiring contract, not user-tunable behavior. */
    crm_pll_config(CRM_PLL_SOURCE_HEXT,72,1,CRM_PLL_FP_4);
    crm_pllu_div_set(CRM_PLL_FU_18);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL,TRUE);
    if(!wait_flag(CRM_PLL_STABLE_FLAG,SET))return failed(M1_CLOCK_PLL);
    crm_auto_step_mode_enable(TRUE);
    crm_sysclk_switch(CRM_SCLK_PLL);
    if(!wait_source(CRM_SCLK_PLL))return failed(M1_CLOCK_PLL_SWITCH);
    crm_auto_step_mode_enable(FALSE);
    system_core_clock_update();
    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);
    if(system_core_clock!=M1_CORE_HZ || clocks.sclk_freq!=M1_CORE_HZ ||
       clocks.apb1_freq!=M1_CORE_HZ/2u || clocks.apb2_freq!=M1_CORE_HZ)
        return M1_CLOCK_RATE;
    return M1_CLOCK_OK;
}
