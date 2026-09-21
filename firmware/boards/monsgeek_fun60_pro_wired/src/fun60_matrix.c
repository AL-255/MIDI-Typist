#include "fun60_board.h"
#include "defaults.h"
#include <string.h>

const fun60_key_t fun60_keys[FUN60_KEY_COUNT] = {
#define FUN60_KEY(index,label,usage,row,channel,led,x,y,width) \
    [index]={label,usage,row,channel,led,x,y,width},
#include "fun60_keys.def"
#undef FUN60_KEY
};

int fun60_sensor(unsigned row,unsigned channel)
{
    for (unsigned i=0;i<FUN60_KEY_COUNT;++i)
        if (fun60_keys[i].row==row && fun60_keys[i].channel==channel) return (int)i;
    return -1;
}

void fun60_encode_lights(const uint8_t rgb[FUN60_KEY_COUNT*3u],uint8_t wire[FUN60_LED_BYTES])
{
    for (unsigned key=0;key<FUN60_KEY_COUNT;++key) {
        const uint8_t grb[]={rgb[3u*key+1u],rgb[3u*key],rgb[3u*key+2u]};
        uint8_t *out=wire+24u*fun60_keys[key].led;
        for (unsigned component=0;component<3;++component)
            for (unsigned bit=0;bit<8;++bit)
                *out++=(grb[component] & (0x80u>>bit)) ? 0xf0u : 0xc0u;
    }
}

static void pulse(const fun60_scan_io_t *io)
{
    io->signal(FUN60_CLOCK,true);
    io->signal(FUN60_DATA,false);
    io->signal(FUN60_CLOCK,false);
    io->delay_us(FUN60_SHIFT_SETTLE_US);
}

bool fun60_scan(const fun60_scan_io_t *io,uint16_t samples[FUN60_KEY_COUNT])
{
    uint16_t frame[FUN60_KEY_COUNT];
    if (!io || !samples || !io->signal || !io->mux || !io->delay_us || !io->adc) return false;
    for (unsigned row=0;row<FUN60_ADC_ROWS;++row) {
        io->signal(FUN60_DATA,true);
        io->signal(FUN60_GATE,true);
        io->mux(0);
        for (unsigned n=0;n<=row;++n) pulse(io);
        io->signal(FUN60_GATE,false);
        io->delay_us(FUN60_ROW_SETTLE_US);
        bool valid=true;
        for (unsigned channel=1;channel<FUN60_MUX_CHANNELS;++channel) {
            io->mux(channel);
            io->delay_us(FUN60_MUX_SETTLE_US);
            uint16_t value;
            if (!io->adc(&value) || value>FUN60_ADC_FULL_SCALE) { valid=false;break; }
            int sensor=fun60_sensor(row,channel);
            if (sensor>=0) frame[sensor]=value;
        }
        io->signal(FUN60_GATE,true);
        io->mux(0);
        /* Original advances/wraps the row BEFORE draining the shift chain.
         * Last row therefore drains another 14 clocks, not zero clocks. */
        unsigned next=(row+1u)%FUN60_ADC_ROWS;
        for (unsigned n=next;n<FUN60_ADC_ROWS;++n) pulse(io);
        if (!valid) return false;
    }
    memcpy(samples,frame,sizeof(frame));
    return true;
}
