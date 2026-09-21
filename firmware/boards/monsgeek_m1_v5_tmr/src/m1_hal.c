/* Application-owned scanner using the official Artery drivers. Stock-derived
 * wiring, custom fixed cadence, and explicit complete-frame ownership.
 * This module is not a startup image and does not enter the factory updater.
 */
#include "m1_hal.h"
#include "defaults.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

static m1_scan_t scan;
static uint16_t row[M1_ADC_RANKS];
static volatile bool initialized, running, busy, healthy, single, paused;
static uint32_t capture_since;
static uint8_t bank;
_Static_assert(M1_CORE_HZ%M1_SCAN_HZ==0,"M1 scan cadence must divide timer clock");
_Static_assert(M1_CORE_HZ/M1_SCAN_HZ<=65536u,"M1 timer period exceeds 16 bits");
_Static_assert(M1_WAKE_SCAN_TIMEOUT_US>
    (M1_BANK_COUNT*M1_ADC_RANKS*2u*1000000ull)/M1_ADC_TRIGGER_HZ,
    "wake timeout must exceed all timer-triggered conversions");

static void select_bank(unsigned index)
{
    unsigned pattern=m1_bank_bits[index];
    static const uint16_t pins[]={GPIO_PINS_9,GPIO_PINS_8,GPIO_PINS_7};
    for(unsigned i=0;i<3;++i) {
        if(pattern&(4u>>i)) gpio_bits_set(GPIOB,pins[i]);
        else gpio_bits_reset(GPIOB,pins[i]);
    }
}
static void arm_row(void)
{
    tmr_counter_enable(TMR3,FALSE);
    dma_channel_enable(DMA1_CHANNEL6,FALSE);
    dma_flag_clear(DMA1_GL6_FLAG);
    select_bank(bank);
    dma_data_number_set(DMA1_CHANNEL6,M1_ADC_RANKS);
    DMA1_CHANNEL6->maddr=(uint32_t)(uintptr_t)row;
    tmr_counter_value_set(TMR3,0);
    tmr_flag_clear(TMR3,TMR_OVF_FLAG);
    dma_channel_enable(DMA1_CHANNEL6,TRUE);
    __DMB();
    tmr_counter_enable(TMR3,TRUE);
}
static void fail(void)
{
    healthy=false; busy=false; running=false; paused=false;
    tmr_counter_enable(TMR6,FALSE);
    tmr_counter_enable(TMR3,FALSE);
    dma_channel_enable(DMA1_CHANNEL6,FALSE);
    adc_enable(ADC1,FALSE);
    dma_flag_clear(DMA1_GL6_FLAG);
    m1_scan_fault(&scan);
}
bool m1_hal_init(void)
{
    if(initialized) m1_hal_stop();
    initialized=healthy=paused=false;
    m1_scan_init(&scan);
    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);
    if(clocks.sclk_freq!=M1_CORE_HZ || clocks.ahb_freq!=M1_CORE_HZ ||
       clocks.apb1_freq!=M1_CORE_HZ/2u || clocks.apb2_freq!=M1_CORE_HZ) return false;
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_TMR3_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_TMR6_PERIPH_CLOCK,TRUE);
    dmamux_enable(DMA1,TRUE);
    tmr_counter_enable(TMR3,FALSE);
    tmr_counter_enable(TMR6,FALSE);
    NVIC_DisableIRQ(DMA1_Channel6_IRQn);
    NVIC_DisableIRQ(TMR6_GLOBAL_IRQn);
    gpio_init_type gpio;
    gpio_default_para_init(&gpio);
    gpio.gpio_mode=GPIO_MODE_ANALOG; gpio.gpio_pull=GPIO_PULL_NONE;
    gpio.gpio_pins=GPIO_PINS_0|GPIO_PINS_1|GPIO_PINS_2|GPIO_PINS_3|
                   GPIO_PINS_4|GPIO_PINS_5|GPIO_PINS_6|GPIO_PINS_7;
    gpio_init(GPIOA,&gpio);
    gpio.gpio_pins=GPIO_PINS_0|GPIO_PINS_1|GPIO_PINS_2|GPIO_PINS_3|GPIO_PINS_4|GPIO_PINS_5;
    gpio_init(GPIOC,&gpio);
    gpio.gpio_pins=GPIO_PINS_0; gpio_init(GPIOB,&gpio);
    gpio.gpio_mode=GPIO_MODE_OUTPUT; gpio.gpio_drive_strength=GPIO_DRIVE_STRENGTH_MODERATE;
    gpio.gpio_pins=GPIO_PINS_7|GPIO_PINS_8|GPIO_PINS_9;
    gpio_init(GPIOB,&gpio); select_bank(0);
    dma_reset(DMA1_CHANNEL6);
    dma_init_type dma;
    dma_default_para_init(&dma);
    dma.direction=DMA_DIR_PERIPHERAL_TO_MEMORY;
    dma.memory_data_width=DMA_MEMORY_DATA_WIDTH_HALFWORD;
    dma.peripheral_data_width=DMA_PERIPHERAL_DATA_WIDTH_HALFWORD;
    dma.memory_inc_enable=TRUE; dma.peripheral_inc_enable=FALSE;
    dma.loop_mode_enable=FALSE; dma.priority=DMA_PRIORITY_VERY_HIGH;
    dma.peripheral_base_addr=(uint32_t)(uintptr_t)&ADC1->odt;
    dma.memory_base_addr=(uint32_t)(uintptr_t)row;
    dma.buffer_size=M1_ADC_RANKS;
    dma_init(DMA1_CHANNEL6,&dma);
    dmamux_init(DMA1MUX_CHANNEL6,DMAMUX_DMAREQ_ID_ADC1);
    dma_interrupt_enable(DMA1_CHANNEL6,DMA_FDT_INT|DMA_DTERR_INT,TRUE);
    adc_reset(ADC1);
    /* Timer base setup generates a software overflow. Do this while the ADC
     * is reset, before enabling its trigger, so it cannot start a stray row. */
    tmr_base_init(TMR3,1,M1_CORE_HZ/M1_ADC_TRIGGER_HZ-1u);
    tmr_cnt_dir_set(TMR3,TMR_COUNT_UP);
    tmr_one_cycle_mode_enable(TMR3,TRUE);
    tmr_overflow_request_source_set(TMR3,TRUE);
    tmr_primary_mode_select(TMR3,TMR_PRIMARY_SEL_OVERFLOW);
    tmr_base_init(TMR6,M1_CORE_HZ/M1_SCAN_HZ-1u,0);
    tmr_cnt_dir_set(TMR6,TMR_COUNT_UP);
    tmr_one_cycle_mode_enable(TMR6,FALSE);
    tmr_flag_clear(TMR6,TMR_OVF_FLAG);
    tmr_interrupt_enable(TMR6,TMR_OVF_INT,TRUE);
    adc_clock_div_set(ADC_DIV_2);
    adc_base_config_type adc;
    adc_base_default_para_init(&adc);
    adc.sequence_mode=TRUE; adc.repeat_mode=FALSE;
    adc.data_align=ADC_RIGHT_ALIGNMENT; adc.ordinary_channel_length=M1_ADC_RANKS;
    adc_base_config(ADC1,&adc);
    for(unsigned i=0;i<M1_ADC_RANKS;++i)
        adc_ordinary_channel_set(ADC1,(adc_channel_select_type)m1_adc_channels[i],i+1u,ADC_SAMPLETIME_7_5);
    adc_ordinary_conversion_trigger_set(ADC1,ADC12_ORDINARY_TRIG_TMR3TRGOUT,TRUE);
    adc_dma_mode_enable(ADC1,TRUE);
    adc_enable(ADC1,TRUE);
    adc_calibration_init(ADC1);
    unsigned waits=M1_ADC_CALIBRATION_WAIT_LOOPS;
    while(adc_calibration_init_status_get(ADC1)!=RESET)
        if(!waits--) { fail(); return false; }
    adc_calibration_start(ADC1); waits=M1_ADC_CALIBRATION_WAIT_LOOPS;
    while(adc_calibration_status_get(ADC1)!=RESET)
        if(!waits--) { fail(); return false; }
    initialized=healthy=true;
    return true;
}
bool m1_hal_start(void)
{
    if(!initialized || !healthy || running || paused) return false;
    /* A completed one-shot owns its frame until consumed. */
    if(single && scan.pending)return false;
    bank=0; busy=single=false; scan.next_bank=0; scan.pending=false;
    adc_enable(ADC1,TRUE);
    NVIC_ClearPendingIRQ(DMA1_Channel6_IRQn);
    NVIC_ClearPendingIRQ(TMR6_GLOBAL_IRQn);
    NVIC_SetPriority(DMA1_Channel6_IRQn,2);
    NVIC_SetPriority(TMR6_GLOBAL_IRQn,2);
    NVIC_EnableIRQ(DMA1_Channel6_IRQn);
    NVIC_EnableIRQ(TMR6_GLOBAL_IRQn);
    tmr_counter_value_set(TMR6,0); tmr_flag_clear(TMR6,TMR_OVF_FLAG);
    running=true;
    tmr_counter_enable(TMR6,TRUE);
    return true;
}
static bool pause_context(void)
{
    return !__get_IPSR() && !__get_BASEPRI() && !__get_FAULTMASK() &&
        !(__get_CONTROL()&1u);
}
bool m1_hal_pause(void)
{
    if(!pause_context())return false;
    uint32_t mask=__get_PRIMASK(); __disable_irq();
    if(!initialized || !healthy || !running || single || paused) {
        __set_PRIMASK(mask); return false;
    }
    /* Do not disguise an already-latched acquisition failure as a save gap. */
    if(dma_interrupt_flag_get(DMA1_DTERR6_FLAG)!=RESET ||
       (busy && tmr_flag_get(TMR6,TMR_OVF_FLAG)!=RESET)) {
        fail(); __set_PRIMASK(mask); return false;
    }
    NVIC_DisableIRQ(DMA1_Channel6_IRQn); NVIC_DisableIRQ(TMR6_GLOBAL_IRQn);
    tmr_counter_enable(TMR6,FALSE); tmr_counter_enable(TMR3,FALSE);
    adc_enable(ADC1,FALSE); /* Stops/resets a partially acquired sequence. */
    dma_channel_enable(DMA1_CHANNEL6,FALSE);
    __DSB();
    if(dma_interrupt_flag_get(DMA1_DTERR6_FLAG)!=RESET) {
        fail(); __set_PRIMASK(mask); return false;
    }
    dma_flag_clear(DMA1_GL6_FLAG);
    tmr_flag_clear(TMR3,TMR_OVF_FLAG); tmr_flag_clear(TMR6,TMR_OVF_FLAG);
    NVIC_ClearPendingIRQ(DMA1_Channel6_IRQn); NVIC_ClearPendingIRQ(TMR6_GLOBAL_IRQn);
    bank=scan.next_bank=0;
    busy=running=scan.pending=scan.battery_valid=false;
    paused=true;
    __DMB(); __set_PRIMASK(mask);
    return true;
}
bool m1_hal_resume(void)
{
    if(!pause_context())return false;
    uint32_t mask=__get_PRIMASK(); __disable_irq();
    if(!initialized || !healthy || !paused) {
        __set_PRIMASK(mask); return false;
    }
    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);
    if(clocks.sclk_freq!=M1_CORE_HZ || clocks.ahb_freq!=M1_CORE_HZ ||
       clocks.apb1_freq!=M1_CORE_HZ/2u || clocks.apb2_freq!=M1_CORE_HZ) {
        __set_PRIMASK(mask); return false;
    }
    /* Retain ADC calibration/rank setup and all power rails. TMR6 starts at
     * zero: ADC gets a full frame period to settle before its first trigger.
     * No pre-pause frame or battery reading is published after this boundary. */
    dma_flag_clear(DMA1_GL6_FLAG);
    tmr_flag_clear(TMR3,TMR_OVF_FLAG);
    paused=false;
    bool started=m1_hal_start();
    __DMB(); __set_PRIMASK(mask);
    return started;
}
bool m1_hal_capture_start(uint32_t now_us)
{
    uint32_t mask=__get_PRIMASK(); __disable_irq();
    if(!initialized || !healthy || running || paused || scan.pending) {
        __set_PRIMASK(mask); return false;
    }
    NVIC_DisableIRQ(TMR6_GLOBAL_IRQn);
    tmr_counter_enable(TMR6,FALSE);
    tmr_flag_clear(TMR6,TMR_OVF_FLAG);
    NVIC_ClearPendingIRQ(TMR6_GLOBAL_IRQn);
    NVIC_ClearPendingIRQ(DMA1_Channel6_IRQn);
    NVIC_SetPriority(DMA1_Channel6_IRQn,2);
    scan.next_bank=bank=0;
    single=running=busy=true;
    capture_since=now_us;
    adc_enable(ADC1,TRUE);
    NVIC_EnableIRQ(DMA1_Channel6_IRQn);
    arm_row();
    __set_PRIMASK(mask);
    return true;
}
bool m1_hal_capture_busy(void) { return single && running && healthy; }
void m1_hal_service(uint32_t now_us)
{
    uint32_t mask=__get_PRIMASK(); __disable_irq();
    if(single && running && healthy && (uint32_t)(now_us-capture_since)>=M1_WAKE_SCAN_TIMEOUT_US)
        fail();
    __set_PRIMASK(mask);
}
void m1_hal_stop(void)
{
    if(!initialized) return;
    NVIC_DisableIRQ(DMA1_Channel6_IRQn); NVIC_DisableIRQ(TMR6_GLOBAL_IRQn);
    tmr_counter_enable(TMR6,FALSE); tmr_counter_enable(TMR3,FALSE);
    dma_channel_enable(DMA1_CHANNEL6,FALSE);
    adc_enable(ADC1,FALSE);
    busy=running=healthy=paused=false; scan.pending=false;
}
void m1_hal_timer_irq(void)
{
    if(tmr_flag_get(TMR6,TMR_OVF_FLAG)==RESET) return;
    tmr_flag_clear(TMR6,TMR_OVF_FLAG);
    if(!running || !healthy || single) return;
    if(busy) { fail(); return; } /* Never silently change velocity's timebase. */
    bank=0; busy=true; arm_row();
}
void m1_hal_dma_irq(void)
{
    if(paused) { dma_flag_clear(DMA1_GL6_FLAG); return; }
    if(dma_interrupt_flag_get(DMA1_DTERR6_FLAG)!=RESET) { fail(); return; }
    if(dma_interrupt_flag_get(DMA1_FDT6_FLAG)==RESET) return;
    tmr_counter_enable(TMR3,FALSE);
    dma_channel_enable(DMA1_CHANNEL6,FALSE);
    dma_flag_clear(DMA1_GL6_FLAG); __DMB();
    if(!running || !busy || !healthy) return;
    if(!m1_scan_bank(&scan,bank,row)) { fail(); return; }
    if(++bank==M1_BANK_COUNT) {
        busy=false;
        if(single) {
            adc_enable(ADC1,FALSE);
            running=false;
        }
    }
    else arm_row();
}
bool m1_hal_frame(uint16_t frame[M1_KEY_COUNT],uint32_t *sequence)
{
    uint32_t mask=__get_PRIMASK(); __disable_irq(); __DMB();
    bool available=healthy && m1_scan_take(&scan,frame,sequence);
    __DMB(); __set_PRIMASK(mask);
    return available;
}
bool m1_hal_healthy(void) { return healthy; }
bool m1_hal_periodic_active(void) { return running && !single && healthy; }
bool m1_hal_battery(uint16_t *adc,uint32_t *sequence)
{
    uint32_t mask=__get_PRIMASK(); __disable_irq(); __DMB();
    bool available=healthy && m1_scan_battery(&scan,adc,sequence);
    __DMB(); __set_PRIMASK(mask);
    return available;
}
uint32_t m1_hal_errors(void)
{
    uint32_t mask=__get_PRIMASK(); __disable_irq();
    uint32_t result=scan.errors;
    __set_PRIMASK(mask); return result;
}
