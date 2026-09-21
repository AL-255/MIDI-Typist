#include "m1_usb_hal.h"
#include "m1_usb.h"
#include "m1_usb_power.h"
#include "m1_power_gpio.h"
#include "m1_board.h"
#include "defaults.h"
#include "usbd_int.h"
#include <string.h>

static otg_core_type core;
static bool owned,starting,keep_pllu,delay_fault,core_fault;
static volatile bool running;
static bool external(void)
{ return gpio_input_data_bit_read(GPIOC,GPIO_PINS_13)==RESET; }
static bool context(void)
{ return !__get_IPSR() && !__get_BASEPRI() && !__get_FAULTMASK() && !(__get_CONTROL()&1u); }
static bool busy(void)
{
    if(running || starting || (SysTick->CTRL&SysTick_CTRL_ENABLE_Msk) ||
       DMA1_CHANNEL1->ctrl_bit.chen || DMA1_CHANNEL2->ctrl_bit.chen ||
       DMA1_CHANNEL3->ctrl_bit.chen || DMA1_CHANNEL6->ctrl_bit.chen ||
       ADC1->ctrl2_bit.adcen || (TMR3->ctrl1&1u) || (TMR6->ctrl1&1u) ||
       spi_i2s_flag_get(SPI2,SPI_I2S_BF_FLAG)==SET ||
       spi_i2s_flag_get(SPI3,SPI_I2S_BF_FLAG)==SET)return true;
    for(int irq=OTGHS_EP1_OUT_IRQn;irq<=OTGHS_IRQn;++irq)
        if(NVIC_GetEnableIRQ((IRQn_Type)irq))return true;
    if(!CRM->ahben1_bit.otghsen || CRM->ahbrst1_bit.otghsrst)return false;
    if(OTG2_GLOBAL->gahbcfg_bit.glbintmsk || OTG2_GLOBAL->gintsts_bit.curmode)return true;
    for(unsigned i=0;i<USB_EPT_MAX_NUM;++i)
        if(USB_INEPT(OTG2_GLOBAL,i)->diepctl_bit.eptena ||
           USB_OUTEPT(OTG2_GLOBAL,i)->doepctl_bit.eptena)return true;
    return false;
}
static bool clocks(void)
{
    /* Check denominators before calling the SDK frequency calculator. */
    if(!CRM->ctrl_bit.hextstbl || !CRM->ctrl_bit.pllstbl ||
       CRM->pllcfg_bit.pllrcs!=CRM_PLL_SOURCE_HEXT || CRM->pllcfg_bit.pllms!=1 ||
       CRM->pllcfg_bit.pllns!=72 || CRM->pllcfg_bit.pllfp!=CRM_PLL_FP_4 ||
       CRM->pllcfg_bit.pllfu!=CRM_PLL_FU_18)return false;
    crm_clocks_freq_type c;crm_clocks_freq_get(&c);
    return c.sclk_freq==M1_CORE_HZ && c.ahb_freq==M1_CORE_HZ &&
           c.apb1_freq==M1_CORE_HZ/2u && c.apb2_freq==M1_CORE_HZ;
}
/* CMSIS cycle counter, not SysTick: USB must not steal the application's time
 * base. Each chunk is <=1 ms, wrap-safe, with a finite stuck-counter path.
 * Only used inside the masked/quiescent SDK initializer; remote wake is off. */
void usb_delay_us(uint32_t us)
{
    if(!starting || us>M1_USB_INIT_DELAY_LIMIT_MS*1000u) { delay_fault=true;return; }
    while(us && !delay_fault) {
        unsigned chunk=us>1000u?1000u:us;
        uint32_t cycles=chunk*(M1_CORE_HZ/1000000u),start=DWT->CYCCNT;
        bool elapsed=false;
        for(uint32_t count=0;count<=cycles;++count)
            if((uint32_t)(DWT->CYCCNT-start)>=cycles) { elapsed=true;break; }
        if(!elapsed || !external())delay_fault=true;
        us-=chunk;
    }
}
void usb_delay_ms(uint32_t ms)
{
    if(ms>M1_USB_INIT_DELAY_LIMIT_MS) { delay_fault=true;return; }
    usb_delay_us(ms*1000u);
}
void __real_usb_global_init(otg_global_type *usb);
void __wrap_usb_global_init(otg_global_type *usb)
{
    if(starting && usb==OTG2_GLOBAL) {
        /* The SDK's ordinary empty 1-ms loop can be optimized away. Supply
         * an actual bounded delay after PHY power-up without editing it. */
        usb->gccfg_bit.pwrdown=TRUE;
        usb_delay_us(M1_USB_PHY_SETTLE_US);
    }
    __real_usb_global_init(usb);
    if(starting && usb==OTG2_GLOBAL &&
       (!usb->grstctl_bit.ahbidle || usb->grstctl_bit.csftrst))core_fault=true;
}
void __real_usb_connect(otg_global_type *usb);
void __wrap_usb_connect(otg_global_type *usb)
{
    /* The SDK connects before returning its unconditional USB_OK. Keep the
     * pull-up disconnected until all board/core postconditions are proven. */
    if(!starting || usb!=OTG2_GLOBAL)__real_usb_connect(usb);
}
static void stop_owned(void)
{
    if(!owned)return;
    running=false;
    for(int irq=OTGHS_EP1_OUT_IRQn;irq<=OTGHS_IRQn;++irq) {
        NVIC_DisableIRQ((IRQn_Type)irq);NVIC_ClearPendingIRQ((IRQn_Type)irq);
    }
    usb_interrupt_disable(OTG2_GLOBAL);
    usb_disconnect(OTG2_GLOBAL);
    /* Reset before reusing static PIO buffers; keep reset asserted while off.
     * No flash, GPIO/rail, radio, scan DMA or shared PLL reconfiguration. */
    crm_periph_reset(CRM_OTGHS_PERIPH_RESET,TRUE);
    __DSB();
    m1_usb_bind(NULL);
    crm_periph_clock_enable(CRM_OTGHS_PERIPH_CLOCK,FALSE);
    if(!keep_pllu)crm_pllu_output_set(FALSE);
    m1_usb_power_invalidate();owned=false;
}
m1_usb_hw_result_t m1_usb_hw_stop(void)
{
    if(!context())return M1_USB_HW_CONTEXT;
    uint32_t mask=__get_PRIMASK();__disable_irq();stop_owned();__set_PRIMASK(mask);
    return M1_USB_HW_OK;
}
bool m1_usb_hw_running(void) { return running; }
void m1_usb_hw_irq(void)
{ if(running)usbd_irq_handler(&core); }
m1_usb_hw_result_t m1_usb_hw_start(bool platform_quiescent)
{
    if(!context())return M1_USB_HW_CONTEXT;
    if(!platform_quiescent || m1_power_gpio_prepared())return M1_USB_HW_BUSY;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    m1_usb_hw_result_t result=M1_USB_HW_OK;
    if(busy())result=M1_USB_HW_BUSY;
    else if(!clocks())result=M1_USB_HW_CLOCK;
    else if(!CRM->ahben1_bit.gpiocen || ((GPIOC->cfgr>>26)&3u)!=GPIO_MODE_INPUT ||
            !external())result=M1_USB_HW_EXTERNAL;
    if(result!=M1_USB_HW_OK) { __set_PRIMASK(mask);return result; }
    keep_pllu=CRM->pllcfg_bit.plluen;starting=true;
    delay_fault=core_fault=false;m1_usb_power_invalidate();
    uint32_t trace=CoreDebug->DEMCR;
    CoreDebug->DEMCR=trace|CoreDebug_DEMCR_TRCENA_Msk;
    uint32_t dwt=DWT->CTRL;
    DWT->CTRL=dwt|DWT_CTRL_CYCCNTENA_Msk;
    if(dwt&DWT_CTRL_NOCYCCNT_Msk)delay_fault=true;
    usb_delay_us(1); /* prove that the counter advances before touching USB */
    if(delay_fault) { result=M1_USB_HW_DELAY;goto finished; }
    owned=true;
    crm_periph_reset(CRM_OTGHS_PERIPH_RESET,TRUE);
    crm_periph_reset(CRM_OTGHS_PERIPH_RESET,FALSE);
    crm_periph_clock_enable(CRM_OTGHS_PERIPH_CLOCK,TRUE);
    crm_pllu_output_set(TRUE);
    bool stable=false;
    for(unsigned i=0;i<AT32_CLOCK_WAIT_LOOPS;++i)
        if(crm_flag_get(CRM_PLLU_STABLE_FLAG)==SET) { stable=true;break; }
    if(!stable)result=M1_USB_HW_PLLU;
    else if(delay_fault)result=M1_USB_HW_DELAY;
    else {
        crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_PLLU);
        crm_usb_phy12_clock_select(CRM_USB_PHY12_CLOCK_HEXT_DIV_1);
        memset(&core,0,sizeof(core));m1_usb_bind(&core.dev);
        (void)usbd_init(&core,USB_HIGH_SPEED_CORE_ID,USB_OTG2_ID,&m1_usb_class,&m1_usb_descriptors);
        if(delay_fault)result=M1_USB_HW_DELAY;
        else if(core_fault || !OTG2_GLOBAL->grstctl_bit.ahbidle ||
                OTG2_GLOBAL->grstctl_bit.csftrst || OTG2_GLOBAL->grstctl_bit.txfflsh ||
                OTG2_GLOBAL->grstctl_bit.rxfflsh || OTG2_GLOBAL->gintsts_bit.curmode ||
                OTG_PCGCCTL(OTG2_GLOBAL)->pcgcctl_bit.stoppclk ||
                !OTG2_GLOBAL->gccfg_bit.pwrdown || core.dev.dma_en)result=M1_USB_HW_CORE;
        if(!external())result=M1_USB_HW_EXTERNAL;
    }
finished:
    DWT->CTRL=dwt;CoreDebug->DEMCR=trace;
    starting=false;
    if(result==M1_USB_HW_OK) {
        NVIC_ClearPendingIRQ(OTGHS_IRQn);
        NVIC_SetPriority(OTGHS_IRQn,0); /* reference IRQ77 priority */
        running=true;usb_connect(OTG2_GLOBAL);NVIC_EnableIRQ(OTGHS_IRQn);
    } else stop_owned();
    __set_PRIMASK(mask);return result;
}
