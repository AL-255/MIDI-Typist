#include "m1_encoder.h"
#include "at32f402_405_conf.h"
#include <stddef.h>

_Static_assert(ENCODER_EVENT_CAPACITY>0 && ENCODER_EVENT_CAPACITY<=255,"encoder queue range");
static keyboard_encoder_t decoder;
static m1_encoder_status_t status;
static uint8_t queue[ENCODER_EVENT_CAPACITY],read_at;
static uint8_t pins(bool *pressed)
{
    unsigned a=gpio_input_data_bit_read(GPIOC,GPIO_PINS_10)==SET;
    unsigned b=gpio_input_data_bit_read(GPIOC,GPIO_PINS_12)==SET;
    *pressed=gpio_input_data_bit_read(GPIOC,GPIO_PINS_11)==RESET;
    return a|(b<<1);
}
void m1_encoder_start(void)
{
    bool pressed;uint8_t phase=pins(&pressed);
    status.queued=read_at=0;status.fault=false;
    status.active=keyboard_encoder_init(&decoder,phase,pressed,M1_SCAN_HZ);
    status.phase=phase;status.pressed=pressed;
}
void m1_encoder_stop(void) { status.active=false;status.queued=read_at=0; }
void m1_encoder_discard(void)
{
    if(__get_IPSR())return;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    if(status.active && !status.fault) {
        bool pressed;uint8_t phase=pins(&pressed);
        (void)keyboard_encoder_init(&decoder,phase,pressed,M1_SCAN_HZ);
        status.phase=phase;status.pressed=pressed;
    }
    status.queued=read_at=0;
    __set_PRIMASK(mask);
}
void m1_encoder_irq(void)
{
    if(!status.active || status.fault)return;
    bool pressed;uint8_t phase=pins(&pressed);
    ++status.samples;
    uint32_t invalid=decoder.invalid_transitions;
    uint8_t events=keyboard_encoder_sample(&decoder,phase,pressed);
    status.invalid+=decoder.invalid_transitions-invalid;
    status.phase=decoder.phase;status.pressed=decoder.button;
    if(events&ENCODER_POSITIVE)++status.positive;
    if(events&ENCODER_NEGATIVE)++status.negative;
    if(!events)return;
    if(status.queued==ENCODER_EVENT_CAPACITY) {
        ++status.overflows;status.fault=true;status.queued=read_at=0;return;
    }
    queue[(read_at+status.queued)%ENCODER_EVENT_CAPACITY]=events;++status.queued;
}
bool m1_encoder_take(uint8_t *events)
{
    if(!events || __get_IPSR())return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    bool ready=status.active && !status.fault && status.queued;
    if(ready) { *events=queue[read_at];read_at=(read_at+1u)%ENCODER_EVENT_CAPACITY;--status.queued; }
    __set_PRIMASK(mask);return ready;
}
bool m1_encoder_status(m1_encoder_status_t *out)
{
    if(!out || __get_IPSR())return false;
    uint32_t mask=__get_PRIMASK();__disable_irq();*out=status;__set_PRIMASK(mask);return true;
}
