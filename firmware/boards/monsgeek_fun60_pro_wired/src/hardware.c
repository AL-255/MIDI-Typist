/* Board wiring and sequencing expressed through official Artery peripherals.
 * No recovered application code, hard-coded RAM state, or factory flash writes.
 * Reference entry points/electrical assumptions: docs/MONSGEEK_FUN60_PRO.md. */
#include "fun60_board.h"
#include "defaults.h"
#include "at32f402_405_conf.h"

static bool initialized, led_in_flight, led_latching;
static uint32_t led_idle_at;
static uint8_t led_wire[FUN60_LED_BYTES] __attribute__((aligned(4)));

static bool expired(uint32_t start,unsigned us)
{
    return (uint32_t)(DWT->CYCCNT-start)>=us*(system_core_clock/1000000u);
}
static void delay_us(unsigned us)
{
    uint32_t start=DWT->CYCCNT;
    while (!expired(start,us)) { __NOP(); }
}
static bool clock_flag(uint32_t flag)
{
    uint32_t start=DWT->CYCCNT;
    while (crm_flag_get(flag)==RESET)
        if (expired(start,FUN60_CLOCK_TIMEOUT_US)) return false;
    return true;
}
static bool clocks(void)
{
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT=0;
    DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    crm_reset();
    system_core_clock_update();
    flash_psr_set(FLASH_WAIT_CYCLE_6);
    crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK,TRUE);
    pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT,TRUE);
    if (!clock_flag(CRM_HEXT_STABLE_FLAG)) return false;
    crm_pll_config(CRM_PLL_SOURCE_HEXT,72,1,CRM_PLL_FP_4);
    crm_pllu_div_set(CRM_PLL_FU_18);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL,TRUE);
    if (!clock_flag(CRM_PLL_STABLE_FLAG)) return false;
    crm_ahb_div_set(CRM_AHB_DIV_1);
    crm_apb1_div_set(CRM_APB1_DIV_2);
    crm_apb2_div_set(CRM_APB2_DIV_1);
    crm_auto_step_mode_enable(TRUE);
    crm_sysclk_switch(CRM_SCLK_PLL);
    uint32_t start=DWT->CYCCNT;
    while (crm_sysclk_switch_status_get()!=CRM_SCLK_PLL)
        if (expired(start,FUN60_CLOCK_TIMEOUT_US)) return false;
    crm_auto_step_mode_enable(FALSE);
    system_core_clock_update();
    return system_core_clock==FUN60_CORE_HZ;
}
static void pins(gpio_type *port,uint16_t mask,gpio_mode_type mode,gpio_pull_type pull)
{
    gpio_init_type cfg;
    gpio_default_para_init(&cfg);
    cfg.gpio_pins=mask;cfg.gpio_mode=mode;cfg.gpio_pull=pull;
    cfg.gpio_out_type=GPIO_OUTPUT_PUSH_PULL;
    gpio_init(port,&cfg);
}
static void scan_signal(fun60_signal_t signal,bool high)
{
    uint16_t mask=(uint16_t)(GPIO_PINS_4<<signal);
    if (high) gpio_bits_set(GPIOA,mask); else gpio_bits_reset(GPIOA,mask);
}
static void mux(unsigned channel)
{
    gpio_bits_reset(GPIOC,GPIO_PINS_6|GPIO_PINS_7|GPIO_PINS_8);
    gpio_bits_set(GPIOC,(uint16_t)((channel&7u)<<6));
}
static bool adc_read(uint16_t *value)
{
    adc_flag_clear(ADC1,ADC_CCE_FLAG);
    adc_ordinary_software_trigger_enable(ADC1,TRUE);
    uint32_t start=DWT->CYCCNT;
    while (adc_flag_get(ADC1,ADC_CCE_FLAG)==RESET)
        if (expired(start,FUN60_ADC_TIMEOUT_US)) return false;
    *value=adc_ordinary_conversion_data_get(ADC1);
    adc_flag_clear(ADC1,ADC_CCE_FLAG);
    return *value<=FUN60_ADC_FULL_SCALE;
}
static bool adc_init(void)
{
    adc_base_config_type cfg;
    crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK,TRUE);
    adc_clock_div_set(ADC_DIV_8);
    adc_base_default_para_init(&cfg);
    adc_base_config(ADC1,&cfg);
    adc_ordinary_channel_set(ADC1,ADC_CHANNEL_2,1,ADC_SAMPLETIME_28_5);
    adc_ordinary_conversion_trigger_set(ADC1,ADC12_ORDINARY_TRIG_SOFTWARE,TRUE);
    adc_enable(ADC1,TRUE);
    adc_calibration_init(ADC1);
    uint32_t start=DWT->CYCCNT;
    while (adc_calibration_init_status_get(ADC1))
        if (expired(start,FUN60_ADC_CAL_TIMEOUT_US)) return false;
    adc_calibration_start(ADC1);
    start=DWT->CYCCNT;
    while (adc_calibration_status_get(ADC1))
        if (expired(start,FUN60_ADC_CAL_TIMEOUT_US)) return false;
    for (unsigned n=0;n<FUN60_ADC_WARMUP_SAMPLES;++n) {
        uint16_t value;
        if (!adc_read(&value)) return false;
    }
    return true;
}
static void light_init(void)
{
    crm_periph_clock_enable(CRM_SPI2_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK,TRUE);
    pins(GPIOA,GPIO_PINS_10,GPIO_MODE_MUX,GPIO_PULL_UP);
    gpio_pin_mux_config(GPIOA,GPIO_PINS_SOURCE10,GPIO_MUX_5);
    spi_init_type cfg;
    spi_default_para_init(&cfg);
    cfg.transmission_mode=SPI_TRANSMIT_HALF_DUPLEX_TX;
    cfg.master_slave_mode=SPI_MODE_MASTER;
    cfg.mclk_freq_division=SPI_MCLK_DIV_16;
    cfg.first_bit_transmission=SPI_FIRST_BIT_MSB;
    cfg.frame_bit_num=SPI_FRAME_8BIT;
    cfg.clock_polarity=SPI_CLOCK_POLARITY_LOW;
    cfg.clock_phase=SPI_CLOCK_PHASE_2EDGE;
    cfg.cs_mode_selection=SPI_CS_SOFTWARE_MODE;
    spi_init(SPI2,&cfg);
    spi_hardware_cs_output_enable(SPI2,TRUE);
    spi_i2s_dma_transmitter_enable(SPI2,TRUE);
    spi_enable(SPI2,TRUE);
    dmamux_enable(DMA1,TRUE);
    dmamux_init(DMA1MUX_CHANNEL1,DMAMUX_DMAREQ_ID_SPI2_TX);
}
bool fun60_hardware_init(void)
{
    initialized=false;led_in_flight=led_latching=false;
    if (!clocks()) return false;
    const crm_periph_clock_type gates[]={CRM_GPIOA_PERIPH_CLOCK,CRM_GPIOB_PERIPH_CLOCK,
        CRM_GPIOC_PERIPH_CLOCK,CRM_GPIOD_PERIPH_CLOCK,CRM_GPIOF_PERIPH_CLOCK};
    gpio_type *const ports[]={GPIOA,GPIOB,GPIOC,GPIOD,GPIOF};
    for (unsigned i=0;i<5;++i) {
        crm_periph_clock_enable(gates[i],TRUE);
        pins(ports[i],GPIO_PINS_ALL,GPIO_MODE_ANALOG,GPIO_PULL_NONE);
    }
    pins(GPIOF,GPIO_PINS_7,GPIO_MODE_OUTPUT,GPIO_PULL_UP);
    if (!adc_init()) return false;
    pins(GPIOC,GPIO_PINS_13,GPIO_MODE_INPUT,GPIO_PULL_UP);
    pins(GPIOA,GPIO_PINS_3,GPIO_MODE_INPUT,GPIO_PULL_UP);
    pins(GPIOB,GPIO_PINS_13,GPIO_MODE_OUTPUT,GPIO_PULL_UP);
    pins(GPIOC,GPIO_PINS_4|GPIO_PINS_5|GPIO_PINS_6|GPIO_PINS_7|GPIO_PINS_8|GPIO_PINS_14,
         GPIO_MODE_OUTPUT,GPIO_PULL_UP);
    pins(GPIOA,GPIO_PINS_4|GPIO_PINS_5|GPIO_PINS_6|GPIO_PINS_7,GPIO_MODE_OUTPUT,GPIO_PULL_UP);
    gpio_bits_set(GPIOC,GPIO_PINS_4|GPIO_PINS_5|GPIO_PINS_14);
    gpio_bits_set(GPIOB,GPIO_PINS_13);
    gpio_bits_set(GPIOA,GPIO_PINS_7);
    scan_signal(FUN60_GATE,true);mux(0);
    light_init();
    initialized=true;
    return true;
}
bool fun60_hardware_scan(uint16_t samples[FUN60_KEY_COUNT])
{
    static const fun60_scan_io_t io={scan_signal,mux,delay_us,adc_read};
    return initialized && fun60_scan(&io,samples);
}
bool fun60_hardware_lights(const uint8_t rgb[FUN60_KEY_COUNT*3u])
{
    if (!initialized || !rgb) return false;
    if (led_in_flight) {
        if (dma_flag_get(DMA1_FDT1_FLAG)==RESET || spi_i2s_flag_get(SPI2,SPI_I2S_BF_FLAG)!=RESET)
            return false;
        dma_channel_enable(DMA1_CHANNEL1,FALSE);
        dma_flag_clear(DMA1_FDT1_FLAG);
        led_idle_at=DWT->CYCCNT;led_latching=true;led_in_flight=false;
    }
    if (led_latching && !expired(led_idle_at,FUN60_LED_LATCH_US)) return false;
    led_latching=false;
    fun60_encode_lights(rgb,led_wire);
    dma_init_type cfg;
    dma_reset(DMA1_CHANNEL1);
    dma_default_para_init(&cfg);
    cfg.buffer_size=sizeof(led_wire);
    cfg.memory_base_addr=(uint32_t)led_wire;
    cfg.peripheral_base_addr=(uint32_t)&SPI2->dt;
    cfg.direction=DMA_DIR_MEMORY_TO_PERIPHERAL;
    cfg.memory_inc_enable=TRUE;
    cfg.memory_data_width=DMA_MEMORY_DATA_WIDTH_BYTE;
    cfg.peripheral_data_width=DMA_PERIPHERAL_DATA_WIDTH_BYTE;
    cfg.priority=DMA_PRIORITY_HIGH;
    dma_init(DMA1_CHANNEL1,&cfg);
    dma_channel_enable(DMA1_CHANNEL1,TRUE);
    led_in_flight=true;
    return true;
}
