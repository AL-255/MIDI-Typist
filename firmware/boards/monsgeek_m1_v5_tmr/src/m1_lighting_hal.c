#include "m1_lighting.h"
#include "defaults.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

enum { STOPPED, LATCH, READY, TRANSFER, DRAIN, FAULT };
static unsigned state;
static bool configured;
static uint8_t encoded[M1_LED_WIRE_BYTES];
static uint32_t since,started,errors;
_Static_assert(M1_LED_SPI_DIV==16u,"SPI enum must match the clock divider");
_Static_assert(M1_LED_TRANSFER_TIMEOUT_US >
    (M1_LED_WIRE_BYTES*8u*1000000ull)/(M1_CORE_HZ/2u/M1_LED_SPI_DIV),
    "LED timeout must exceed the entire physical frame");

static void pin_low(void)
{
    gpio_bits_reset(GPIOA,GPIO_PINS_10);
    gpio_init_type gpio;
    gpio_default_para_init(&gpio);
    gpio.gpio_pins=GPIO_PINS_10; gpio.gpio_mode=GPIO_MODE_OUTPUT;
    gpio.gpio_pull=GPIO_PULL_NONE; gpio.gpio_out_type=GPIO_OUTPUT_PUSH_PULL;
    gpio.gpio_drive_strength=GPIO_DRIVE_STRENGTH_MODERATE;
    gpio_init(GPIOA,&gpio);
}
static void pin_spi(void)
{
    gpio_init_type gpio;
    gpio_default_para_init(&gpio);
    gpio.gpio_pins=GPIO_PINS_10; gpio.gpio_mode=GPIO_MODE_MUX;
    gpio.gpio_pull=GPIO_PULL_UP; gpio.gpio_out_type=GPIO_OUTPUT_PUSH_PULL;
    gpio.gpio_drive_strength=GPIO_DRIVE_STRENGTH_MODERATE;
    gpio_pin_mux_config(GPIOA,GPIO_PINS_SOURCE10,GPIO_MUX_5);
    gpio_init(GPIOA,&gpio);
}
void m1_lighting_stop(void)
{
    if(configured) {
        spi_i2s_dma_transmitter_enable(SPI2,FALSE);
        dma_channel_enable(DMA1_CHANNEL1,FALSE);
        spi_enable(SPI2,FALSE);
        dma_flag_clear(DMA1_GL1_FLAG);
        pin_low();
    }
    state=STOPPED;
}
static void fault(void)
{
    m1_lighting_stop(); ++errors; state=FAULT;
}
bool m1_lighting_init(uint32_t now_us)
{
    m1_lighting_stop(); errors=0;
    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);
    if(clocks.sclk_freq!=M1_CORE_HZ || clocks.ahb_freq!=M1_CORE_HZ ||
       clocks.apb1_freq!=M1_CORE_HZ/2u) { state=FAULT; ++errors; return false; }
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_SPI2_PERIPH_CLOCK,TRUE);
    configured=true;
    NVIC_DisableIRQ(DMA1_Channel1_IRQn);
    dmamux_enable(DMA1,TRUE);
    dma_reset(DMA1_CHANNEL1);
    spi_i2s_reset(SPI2);
    pin_low();
    spi_init_type spi;
    spi_default_para_init(&spi);
    spi.transmission_mode=SPI_TRANSMIT_HALF_DUPLEX_TX;
    spi.master_slave_mode=SPI_MODE_MASTER;
    spi.mclk_freq_division=SPI_MCLK_DIV_16;
    spi.first_bit_transmission=SPI_FIRST_BIT_MSB;
    spi.frame_bit_num=SPI_FRAME_8BIT;
    spi.clock_polarity=SPI_CLOCK_POLARITY_LOW;
    spi.clock_phase=SPI_CLOCK_PHASE_2EDGE;
    spi.cs_mode_selection=SPI_CS_SOFTWARE_MODE;
    spi_init(SPI2,&spi);
    spi_hardware_cs_output_enable(SPI2,TRUE);
    /* Keep PA10 actively low while the LED supply settles. No DMA starts. */
    since=now_us; state=LATCH;
    return true;
}
bool m1_lighting_healthy(void) { return state!=STOPPED && state!=FAULT; }
bool m1_lighting_ready(void) { return state==READY; }
uint32_t m1_lighting_errors(void) { return errors; }
void m1_lighting_service(uint32_t now_us)
{
    if(state==TRANSFER || state==DRAIN) {
        if(dma_flag_get(DMA1_DTERR1_FLAG)!=RESET ||
           spi_i2s_flag_get(SPI2,SPI_MMERR_FLAG)!=RESET ||
           (uint32_t)(now_us-started)>=M1_LED_TRANSFER_TIMEOUT_US) { fault(); return; }
        if(state==TRANSFER && dma_flag_get(DMA1_FDT1_FLAG)!=RESET) {
            spi_i2s_dma_transmitter_enable(SPI2,FALSE);
            dma_channel_enable(DMA1_CHANNEL1,FALSE);
            dma_flag_clear(DMA1_GL1_FLAG);
            state=DRAIN;
        }
        /* DMA completion only means the final byte reached SPI's data register.
         * Do not detach the pin or reuse storage until the shifter is empty. */
        if(state==DRAIN && spi_i2s_flag_get(SPI2,SPI_I2S_TDBE_FLAG)!=RESET &&
           spi_i2s_flag_get(SPI2,SPI_I2S_BF_FLAG)==RESET) {
            spi_enable(SPI2,FALSE);
            pin_low(); since=now_us; state=LATCH;
        }
    }
    if(state==LATCH && (uint32_t)(now_us-since)>=M1_LED_LATCH_US)state=READY;
}
bool m1_lighting_offer(const uint8_t *rgb,size_t length,uint32_t now_us)
{
    m1_lighting_service(now_us);
    if(!m1_lighting_ready() || !m1_lighting_encode(rgb,length,encoded,sizeof(encoded)))return false;
    dma_reset(DMA1_CHANNEL1);
    dmamux_init(DMA1MUX_CHANNEL1,DMAMUX_DMAREQ_ID_SPI2_TX);
    dma_init_type dma;
    dma_default_para_init(&dma);
    dma.direction=DMA_DIR_MEMORY_TO_PERIPHERAL;
    dma.memory_base_addr=(uint32_t)(uintptr_t)encoded;
    dma.peripheral_base_addr=(uint32_t)(uintptr_t)&SPI2->dt;
    dma.buffer_size=M1_LED_WIRE_BYTES;
    dma.memory_data_width=DMA_MEMORY_DATA_WIDTH_BYTE;
    dma.peripheral_data_width=DMA_PERIPHERAL_DATA_WIDTH_BYTE;
    dma.memory_inc_enable=TRUE; dma.peripheral_inc_enable=FALSE;
    dma.loop_mode_enable=FALSE; dma.priority=DMA_PRIORITY_HIGH;
    dma_init(DMA1_CHANNEL1,&dma);
    started=now_us; state=TRANSFER;
    spi_enable(SPI2,TRUE); pin_spi();
    spi_i2s_dma_transmitter_enable(SPI2,TRUE);
    __DMB();
    dma_channel_enable(DMA1_CHANNEL1,TRUE);
    return true;
}
