/* Deterministic desktop board driver. No host keystrokes, USB or real flash.
 * Demonstrates that a port supplies samples, time, sinks and storage only. */
#include "keyboard_app.h"
#include "keyboard_sample.h"
#include "synthetic_board.h"
#include <stdio.h>
#include <string.h>

static keyboard_raw_t raw;
static keyboard_midi_t midi;
static keyboard_menu_t menu;
static keyboard_calibration_t cal;
static keyboard_app_t app;
static uint16_t adc[SYN_COUNT],samples[SYN_COUNT],lo[SYN_COUNT],hi[SYN_COUNT];
static uint16_t saved_lo[SYN_COUNT],saved_hi[SYN_COUNT];
static uint8_t rgb[LIGHTING_FRAME_SIZE];
static uint32_t ticks;
static bool saved;
static bool load(uint8_t profile,uint8_t count,uint16_t *lower,uint16_t *upper)
{
    if(!saved || profile!=SYN_PROFILE || count!=SYN_COUNT) return false;
    memcpy(lower,saved_lo,sizeof(saved_lo)); memcpy(upper,saved_hi,sizeof(saved_hi)); return true;
}
static keyboard_save_result_t save(const keyboard_calibration_t *s)
{
    memcpy(saved_lo,s->lower,sizeof(saved_lo)); memcpy(saved_hi,s->upper,sizeof(saved_hi));
    saved=true; puts("CALIBRATION SAVED (simulated RAM storage)"); return KEYBOARD_SAVE_COMPLETE;
}
static bool clear(void) { saved=false; return true; }
static bool send_keyboard(const keyboard_report_t *s)
{
    printf("HID %02x",s->modifiers);
    for(unsigned i=0;i<sizeof(s->keys);++i) printf(" %02x",s->keys[i]);
    putchar('\n'); return true;
}
static bool send_midi(uint8_t cin,uint8_t status,uint8_t a,uint8_t b)
{
    printf("MIDI %02x %02x %02x %02x\n",cin,status,a,b); return true;
}
static void log_line(const char *text) { fputs(text,stdout); }
static const keyboard_app_ops_t ops={load,save,clear,NULL,log_line};
static void step(unsigned ms)
{
    for(unsigned i=0;i<ms*2u;++i) {
        for(unsigned k=0;k<SYN_COUNT;++k)
            (void)keyboard_sample_normalize(adc[k],0,65535,&samples[k]);
        keyboard_app_frame(&app,samples,SYN_COUNT,SYN_PROFILE,lo,hi,true,ticks/2u);
        keyboard_app_service(&app,ticks/2u,true,send_keyboard,send_midi);
        keyboard_app_lights(&app,lo,hi,rgb,ticks/2u);
        ++ticks;
    }
}
int main(void)
{
    synthetic_board_init();
    for(unsigned i=0;i<SYN_COUNT;++i) { lo[i]=SYNTHETIC_CALIBRATION_LOWER_RAW; hi[i]=4096; }
    keyboard_app_init(&app,&raw,&midi,&menu,&cal,&ops);
    puts("MIDI-Typist synthetic board: 104 keys, ascending 16-bit ADC, 2kHz");
    puts("set SENSOR ADC | step MILLISECONDS | status | cfg commands | quit");
    puts("Sensors: 0 Fn, 1 Tab, 2 Enter, 3 Space, 4 LeftShift, 5 S, 6 E, 8 H, 9 P");
    step(1);
    char line[128],extra;
    while(fgets(line,sizeof(line),stdin)) {
        unsigned sensor,value;
        line[strcspn(line,"\r\n")]='\0';
        uint32_t ack=0; uint8_t result=0;
        if(keyboard_app_command(&app,line,ticks/2u,true,&ack,&result)) {
            printf("ACK %u %u\n",ack,result); continue;
        }
        if(sscanf(line,"set %u %u %c",&sensor,&value,&extra)==2 && sensor<SYN_COUNT && value<=65535)
            adc[sensor]=value;
        else if(sscanf(line,"step %u %c",&value,&extra)==1 && value<=60000u) step(value);
        else if(!strcmp(line,"status"))
            printf("mode=%s root=%u scale=%u lower=%u sustain=%u armed=%u calibration=%u\n",
                midi.mode?"MIDI":"KEYBOARD",midi.music.root,midi.music.scale,midi.lower_muted,midi.sustain,raw.armed,cal.state);
        else if(!strcmp(line,"quit")) break;
        else puts("ERR command");
    }
    return 0;
}
