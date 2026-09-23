#include "m1_usb_power.h"
#include "m1_board.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"
#include "at32f402_405_usb.h"
#include "m1_radio.h"

static bool reduced;
_Static_assert(USB_EPT_MAX_NUM==8,"check all hardware endpoints before PHY power-down");

void m1_usb_power_invalidate(void) { reduced=false; }
static bool external_power(void)
{ return gpio_input_data_bit_read(GPIOC,GPIO_PINS_13)==RESET; }
bool m1_usb_power_ready(void)
{
    return reduced && CRM->ahben1_bit.gpiocen && CRM->ahben1_bit.otghsen &&
        !external_power() && OTG2_GLOBAL->gccfg_bit.wait_clk_rcv &&
        !OTG2_GLOBAL->gccfg_bit.pwrdown && OTG_PCGCCTL(OTG2_GLOBAL)->pcgcctl_bit.stoppclk;
}
static bool busy(void)
{
    /* Masking interrupts across the SDK's bounded wait is safe only after
     * time-sensitive acquisition and both output DMA paths have stopped. */
    if(DMA1_CHANNEL1->ctrl_bit.chen || DMA1_CHANNEL2->ctrl_bit.chen ||
       DMA1_CHANNEL3->ctrl_bit.chen || DMA1_CHANNEL6->ctrl_bit.chen ||
       ADC1->ctrl2_bit.adcen || (TMR3->ctrl1&1u) || (TMR6->ctrl1&1u) ||
       spi_i2s_flag_get(SPI2,SPI_I2S_BF_FLAG)==SET ||
       !m1_radio_bus_idle())return true;
    if(NVIC_GetEnableIRQ(OTGHS_IRQn) || NVIC_GetEnableIRQ(OTGHS_WKUP_IRQn) ||
       NVIC_GetEnableIRQ(OTGHS_EP1_IN_IRQn) || NVIC_GetEnableIRQ(OTGHS_EP1_OUT_IRQn))return true;
    if(!CRM->ahben1_bit.otghsen)return false; /* cold core: no live register reads */
    if(OTG2_GLOBAL->gahbcfg_bit.glbintmsk || OTG2_GLOBAL->gintsts_bit.curmode)return true;
    for(unsigned i=0;i<USB_EPT_MAX_NUM;++i)
        if(USB_INEPT(OTG2_GLOBAL,i)->diepctl_bit.eptena ||
           USB_OUTEPT(OTG2_GLOBAL,i)->doepctl_bit.eptena)return true;
    return false;
}
m1_usb_power_result_t m1_usb_power_down(bool platform_quiescent)
{
    if(__get_IPSR() || __get_BASEPRI() || __get_FAULTMASK() || (__get_CONTROL()&1u))
        return M1_USB_POWER_CONTEXT;
    if(!platform_quiescent)return M1_USB_POWER_BUSY;
    uint32_t mask=__get_PRIMASK(); __disable_irq();
    m1_usb_power_result_t result=M1_USB_POWER_OK;
    crm_clocks_freq_type c;
    crm_clocks_freq_get(&c);
    /* The SDK's PLL-only path has an unbounded PLLU wait. Require the verified
     * HEXT path instead; it also matches the reference's final OTGHS mux zero. */
    if(!CRM->ctrl_bit.hextstbl || c.sclk_freq!=M1_CORE_HZ || c.ahb_freq!=M1_CORE_HZ)
        result=M1_USB_POWER_CLOCK;
    else if(!CRM->ahben1_bit.gpiocen || external_power())result=M1_USB_POWER_EXTERNAL;
    else if(busy())result=M1_USB_POWER_BUSY;
    else if(!m1_usb_power_ready()) {
        reduced=false;
        /* The USB owner leaves a stopped core in reset. Only release it after
         * the battery/quiescence checks, before the SDK configures PHY sleep. */
        if(CRM->ahbrst1_bit.otghsrst)crm_periph_reset(CRM_OTGHS_PERIPH_RESET,FALSE);
        reduce_power_consumption(); /* pinned, unmodified official SDK */
        if(!OTG_DEVICE(OTG2_GLOBAL)->dsts_bit.suspsts)result=M1_USB_POWER_TIMEOUT;
        else if(external_power())result=M1_USB_POWER_EXTERNAL;
        else reduced=true;
    }
    __set_PRIMASK(mask);
    return result;
}
