#include "m1_radio.h"
#include "m1_board.h"
#include "defaults.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"
#include <string.h>

enum { OFF, START_PULSE, READY, TRANSFER, DRAIN, COMPLETE, FAULT };
static unsigned state;
static bool configured;
static uint32_t since,errors;
static size_t size;
static _Alignas(4) uint8_t tx[M1_RADIO_BUFFER_BYTES],rx[M1_RADIO_BUFFER_BYTES];
_Static_assert(M1_RADIO_SPI_DIV==16u,"SPI divider enum mismatch");
_Static_assert(M1_RADIO_TRANSFER_TIMEOUT_US>
    (M1_RADIO_BUFFER_BYTES*8u*1000000ull)/(M1_CORE_HZ/2u/M1_RADIO_SPI_DIV),
    "radio timeout must exceed a full frame");

static void pin(gpio_type *port,uint16_t pins,gpio_mode_type mode,gpio_pull_type pull)
{
    gpio_init_type gpio;
    gpio_default_para_init(&gpio);
    gpio.gpio_pins=pins; gpio.gpio_mode=mode; gpio.gpio_pull=pull;
    gpio.gpio_out_type=GPIO_OUTPUT_PUSH_PULL;
    gpio.gpio_drive_strength=GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init(port,&gpio);
}
static void dma_off(void)
{
    spi_i2s_dma_transmitter_enable(SPI3,FALSE);
    spi_i2s_dma_receiver_enable(SPI3,FALSE);
    dma_channel_enable(DMA1_CHANNEL2,FALSE);
    dma_channel_enable(DMA1_CHANNEL3,FALSE);
    dma_flag_clear(DMA1_GL2_FLAG|DMA1_GL3_FLAG);
}
void m1_radio_stop(void)
{
    if(configured) {
        dma_off(); spi_enable(SPI3,FALSE);
        gpio_bits_set(GPIOA,GPIO_PINS_15);
    }
    state=OFF; size=0;
}
static void fault(void) { m1_radio_stop(); ++errors; state=FAULT; }
bool m1_radio_init(uint32_t now)
{
    if(state!=OFF && state!=FAULT)return false;
    m1_radio_stop(); errors=0;
    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);
    if(clocks.sclk_freq!=M1_CORE_HZ || clocks.ahb_freq!=M1_CORE_HZ ||
       clocks.apb1_freq!=M1_CORE_HZ/2u) { ++errors; state=FAULT; return false; }
    crm_periph_clock_enable(CRM_SPI3_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_GPIOD_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK,TRUE);
    configured=true;
    NVIC_DisableIRQ(DMA1_Channel2_IRQn); NVIC_DisableIRQ(DMA1_Channel3_IRQn);
    dmamux_enable(DMA1,TRUE);
    dma_reset(DMA1_CHANNEL2); dma_reset(DMA1_CHANNEL3);
    spi_i2s_reset(SPI3);
    pin(GPIOD,GPIO_PINS_2,GPIO_MODE_INPUT,GPIO_PULL_UP);
    gpio_bits_set(GPIOA,GPIO_PINS_15);
    pin(GPIOA,GPIO_PINS_15,GPIO_MODE_OUTPUT,GPIO_PULL_UP);
    pin(GPIOB,GPIO_PINS_3|GPIO_PINS_4|GPIO_PINS_5,GPIO_MODE_MUX,GPIO_PULL_NONE);
    gpio_pin_mux_config(GPIOB,GPIO_PINS_SOURCE3,GPIO_MUX_6);
    gpio_pin_mux_config(GPIOB,GPIO_PINS_SOURCE4,GPIO_MUX_6);
    gpio_pin_mux_config(GPIOB,GPIO_PINS_SOURCE5,GPIO_MUX_6);
    spi_init_type spi;
    spi_default_para_init(&spi);
    spi.transmission_mode=SPI_TRANSMIT_FULL_DUPLEX;
    spi.master_slave_mode=SPI_MODE_MASTER;
    spi.mclk_freq_division=SPI_MCLK_DIV_16;
    spi.first_bit_transmission=SPI_FIRST_BIT_MSB;
    spi.frame_bit_num=SPI_FRAME_8BIT;
    spi.clock_polarity=SPI_CLOCK_POLARITY_LOW;
    spi.clock_phase=SPI_CLOCK_PHASE_2EDGE;
    spi.cs_mode_selection=SPI_CS_SOFTWARE_MODE;
    spi_init(SPI3,&spi);
    spi_enable(SPI3,TRUE);
    /* Match the original PA15 pulse without blocking the foreground owner. */
    gpio_bits_reset(GPIOA,GPIO_PINS_15);
    since=now; state=START_PULSE;
    return true;
}
bool m1_radio_bus_idle(void)
{ return spi_i2s_flag_get(SPI3,SPI_I2S_BF_FLAG)!=SET; }
bool m1_radio_ready(void) { return state==READY; }
bool m1_radio_healthy(void) { return state!=OFF && state!=FAULT; }
uint32_t m1_radio_errors(void) { return errors; }
bool m1_radio_data_pending(void)
{ return state==READY && gpio_input_data_bit_read(GPIOD,GPIO_PINS_2)==RESET; }
void m1_radio_service(uint32_t now)
{
    if(state==START_PULSE) {
        if((uint32_t)(now-since)>=M1_RADIO_START_PULSE_US) {
            gpio_bits_set(GPIOA,GPIO_PINS_15); state=READY;
        }
        return;
    }
    if(state!=TRANSFER && state!=DRAIN)return;
    if(dma_flag_get(DMA1_DTERR2_FLAG|DMA1_DTERR3_FLAG)!=RESET ||
       spi_i2s_flag_get(SPI3,SPI_MMERR_FLAG|SPI_I2S_ROERR_FLAG)!=RESET ||
       (uint32_t)(now-since)>=M1_RADIO_TRANSFER_TIMEOUT_US) { fault(); return; }
    if(state==TRANSFER && dma_flag_get(DMA1_FDT2_FLAG)!=RESET &&
       dma_flag_get(DMA1_FDT3_FLAG)!=RESET) {
        dma_off(); state=DRAIN;
    }
    if(state==DRAIN && spi_i2s_flag_get(SPI3,SPI_I2S_TDBE_FLAG)!=RESET &&
       spi_i2s_flag_get(SPI3,SPI_I2S_BF_FLAG)==RESET) {
        __DMB(); gpio_bits_set(GPIOA,GPIO_PINS_15); state=COMPLETE;
    }
}
bool m1_radio_exchange(const m1_radio_packet_t *packet,uint32_t now)
{
    if(!packet || packet->size<4u || packet->size>M1_RADIO_BUFFER_BYTES || (packet->size&3u))return false;
    m1_radio_service(now);
    if(state!=READY)return false;
    /* READY has no outstanding clocks or DMA: busy here is an ownership or
     * peripheral fault, not a reason to defer forever without a timeout. */
    if(spi_i2s_flag_get(SPI3,SPI_MMERR_FLAG|SPI_I2S_ROERR_FLAG|SPI_I2S_BF_FLAG)!=RESET ||
       DMA1_CHANNEL2->ctrl_bit.chen || DMA1_CHANNEL3->ctrl_bit.chen) { fault(); return false; }
    size=packet->size;
    memcpy(tx,packet->bytes,size); memset(rx,0,sizeof(rx));
    dma_reset(DMA1_CHANNEL2); dma_reset(DMA1_CHANNEL3);
    dma_init_type dma;
    dma_default_para_init(&dma);
    dma.direction=DMA_DIR_PERIPHERAL_TO_MEMORY;
    dma.memory_base_addr=(uint32_t)(uintptr_t)rx;
    dma.peripheral_base_addr=(uint32_t)(uintptr_t)&SPI3->dt;
    dma.buffer_size=size;
    dma.memory_data_width=DMA_MEMORY_DATA_WIDTH_BYTE;
    dma.peripheral_data_width=DMA_PERIPHERAL_DATA_WIDTH_BYTE;
    dma.memory_inc_enable=TRUE; dma.peripheral_inc_enable=FALSE;
    dma.loop_mode_enable=FALSE; dma.priority=DMA_PRIORITY_HIGH;
    dmamux_init(DMA1MUX_CHANNEL3,DMAMUX_DMAREQ_ID_SPI3_RX);
    dma_init(DMA1_CHANNEL3,&dma);
    dma.direction=DMA_DIR_MEMORY_TO_PERIPHERAL;
    dma.memory_base_addr=(uint32_t)(uintptr_t)tx;
    dmamux_init(DMA1MUX_CHANNEL2,DMAMUX_DMAREQ_ID_SPI3_TX);
    dma_init(DMA1_CHANNEL2,&dma);
    since=now; state=TRANSFER;
    /* Arm RX before asserting select and enabling TX. Both buffers stay owned
     * until RX, TX and the last SPI bit have completed. No ISR mutates them. */
    __DMB();
    dma_channel_enable(DMA1_CHANNEL3,TRUE);
    spi_i2s_dma_receiver_enable(SPI3,TRUE);
    gpio_bits_reset(GPIOA,GPIO_PINS_15);
    spi_i2s_dma_transmitter_enable(SPI3,TRUE);
    dma_channel_enable(DMA1_CHANNEL2,TRUE);
    return true;
}
bool m1_radio_take(uint8_t *out,size_t capacity,size_t *length)
{
    if(state!=COMPLETE || !out || !length || capacity<size)return false;
    memcpy(out,rx,size); *length=size; state=READY;
    return true;
}
bool m1_radio_quiesce(bool peer_asleep)
{
    if(!peer_asleep || !configured || (state!=READY && state!=OFF && state!=FAULT))return false;
    if(spi_i2s_flag_get(SPI3,SPI_I2S_BF_FLAG)!=RESET ||
       DMA1_CHANNEL2->ctrl_bit.chen || DMA1_CHANNEL3->ctrl_bit.chen)return false;
    m1_radio_stop();
    gpio_bits_reset(GPIOD,GPIO_PINS_2);
    gpio_bits_reset(GPIOB,GPIO_PINS_3|GPIO_PINS_4|GPIO_PINS_5);
    pin(GPIOD,GPIO_PINS_2,GPIO_MODE_OUTPUT,GPIO_PULL_NONE);
    pin(GPIOB,GPIO_PINS_3|GPIO_PINS_4|GPIO_PINS_5,GPIO_MODE_OUTPUT,GPIO_PULL_NONE);
    pin(GPIOA,GPIO_PINS_15,GPIO_MODE_OUTPUT,GPIO_PULL_NONE);
    return true;
}
