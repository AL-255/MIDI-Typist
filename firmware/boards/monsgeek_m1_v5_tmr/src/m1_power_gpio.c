#include "m1_power_gpio.h"
#include "m1_usb_power.h"
#include "m1_usb_hal.h"
#include "m1_board.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"
#include "at32f402_405_usb.h"
#include "m1_radio.h"

static bool prepared;
static bool context(void)
{ return !__get_IPSR() && !__get_BASEPRI() && !__get_FAULTMASK() && !(__get_CONTROL()&1u); }
static bool idle(void)
{
    crm_clocks_freq_type clocks;crm_clocks_freq_get(&clocks);
    if(clocks.sclk_freq!=M1_CORE_HZ || clocks.ahb_freq!=M1_CORE_HZ ||
       clocks.apb1_freq!=M1_CORE_HZ/2u || clocks.apb2_freq!=M1_CORE_HZ ||
       m1_usb_hw_running() || DMA1_CHANNEL1->ctrl_bit.chen || DMA1_CHANNEL2->ctrl_bit.chen ||
       DMA1_CHANNEL3->ctrl_bit.chen || DMA1_CHANNEL6->ctrl_bit.chen ||
       ADC1->ctrl2_bit.adcen || (TMR3->ctrl1&1u) || (TMR6->ctrl1&1u) ||
       spi_i2s_flag_get(SPI2,SPI_I2S_BF_FLAG)==SET ||
       !m1_radio_bus_idle() ||
       NVIC_GetEnableIRQ(OTGHS_IRQn) || NVIC_GetEnableIRQ(OTGHS_WKUP_IRQn) ||
       NVIC_GetEnableIRQ(OTGHS_EP1_IN_IRQn) || NVIC_GetEnableIRQ(OTGHS_EP1_OUT_IRQn))return false;
    /* Never inspect an unclocked/reset core. A stopped PHY on cable arrival
     * is sufficient for GPIO restoration, not permission to resume USB. */
    if(!CRM->ahben1_bit.otghsen || CRM->ahbrst1_bit.otghsrst)return true;
    if(OTG2_GLOBAL->gahbcfg_bit.glbintmsk || OTG2_GLOBAL->gintsts_bit.curmode)return false;
    for(unsigned i=0;i<USB_EPT_MAX_NUM;++i)
        if(USB_INEPT(OTG2_GLOBAL,i)->diepctl_bit.eptena ||
           USB_OUTEPT(OTG2_GLOBAL,i)->doepctl_bit.eptena)return false;
    return true;
}
static void configure(gpio_type *port,uint16_t pin,gpio_mode_type mode,gpio_pull_type pull)
{
    gpio_init_type gpio;gpio_default_para_init(&gpio);
    gpio.gpio_pins=pin;gpio.gpio_mode=mode;gpio.gpio_pull=pull;
    gpio.gpio_out_type=GPIO_OUTPUT_PUSH_PULL;
    gpio_init(port,&gpio);
}
static void clocks(void)
{
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK,TRUE);
}
bool m1_power_gpio_prepared(void) { return prepared; }
bool m1_power_gpio_prepare(bool platform_quiescent)
{
    if(!platform_quiescent || prepared || !context())return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    bool ok=idle() && CRM->ahben1_bit.gpiocen &&
        ((GPIOC->cfgr>>26)&3u)==GPIO_MODE_INPUT && m1_usb_power_ready();
    if(ok) {
        clocks();
        /* Pin roles/order from 0x08017BCC. PA11's latch is deliberately left
         * unchanged; its board-level function is not guessed. */
        configure(GPIOB,GPIO_PINS_12,GPIO_MODE_INPUT,GPIO_PULL_NONE);
        configure(GPIOB,GPIO_PINS_10,GPIO_MODE_OUTPUT,GPIO_PULL_NONE);
        gpio_bits_reset(GPIOB,GPIO_PINS_10);
        configure(GPIOA,GPIO_PINS_11,GPIO_MODE_OUTPUT,GPIO_PULL_NONE);
        prepared=true;
    }
    __set_PRIMASK(mask);return ok;
}
bool m1_power_gpio_restore(bool platform_quiescent,uint8_t *encoder_phase)
{
    if(!platform_quiescent || !encoder_phase || !context())return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    bool ok=idle();
    if(ok) {
        clocks();
        configure(GPIOB,GPIO_PINS_12,GPIO_MODE_OUTPUT,GPIO_PULL_NONE);
        gpio_bits_reset(GPIOB,GPIO_PINS_12);
        configure(GPIOB,GPIO_PINS_10,GPIO_MODE_INPUT,GPIO_PULL_UP);
        configure(GPIOA,GPIO_PINS_11,GPIO_MODE_INPUT,GPIO_PULL_UP);
        configure(GPIOC,GPIO_PINS_10,GPIO_MODE_INPUT,GPIO_PULL_UP);
        configure(GPIOC,GPIO_PINS_12,GPIO_MODE_INPUT,GPIO_PULL_UP);
        configure(GPIOC,GPIO_PINS_11,GPIO_MODE_INPUT,GPIO_PULL_UP);
        unsigned a=gpio_input_data_bit_read(GPIOC,GPIO_PINS_10)==SET;
        unsigned b=gpio_input_data_bit_read(GPIOC,GPIO_PINS_12)==SET;
        *encoder_phase=a|(b<<1);prepared=false;
    }
    __set_PRIMASK(mask);return ok;
}
bool m1_power_gpio_switches(uint8_t *switches)
{
    if(!switches || !context() || !CRM->ahben1_bit.gpiocen)return false;
    const unsigned pins[]={10,12,11};unsigned value=0;
    for(unsigned i=0;i<3;++i)
        if(((GPIOC->cfgr>>(pins[i]*2u))&3u)!=GPIO_MODE_INPUT)return false;
    for(unsigned i=0;i<3;++i)
        value|=(gpio_input_data_bit_read(GPIOC,1u<<pins[i])==SET)<<i;
    *switches=value;return true;
}
