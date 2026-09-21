/* Deliberately not a Huntsman: 104 keys, unrelated key IDs/layout ID,
 * linear RGB framebuffer, 2 kHz acquisition and a different lower group.
 * Includes only the public application headers, no vendor SDK or board data. */
#include "synthetic_board.h"
#include "defaults.h"
#include "keyboard_layout.h"
#include "keyboard_lighting.h"
#include <stddef.h>
#include <string.h>

static keyboard_action_t actions[SYN_COUNT][2];
static const uint16_t levels[11]=SYNTHETIC_ACTUATION_LEVELS;
static const uint8_t keymap[256]={
#define KEYMAP(key,usage) [key]=usage,
#include "../config/keymap.def"
#undef KEYMAP
};
static keyboard_layout_t layout;
static keyboard_layout_t small_layout;
static uint8_t key_id(unsigned sensor) { return (sensor*73u+19u)%251u+1u; }
void synthetic_rate(uint32_t hz) { layout.sample_hz=hz; }
void synthetic_board_init(void)
{
    static const uint8_t usage[]={0,0x2b,0x28,0x2c,0,0x16,0x08,0x0d,0x0b,0x13,0x17,
                                0x06,0x0e,0x0f,0x29,0x39,0,0,0,0,0};
    static const uint8_t mod[]={0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,1,8,4,64,16};
    for(unsigned i=0;i<SYN_COUNT;++i) {
        actions[i][0]=(keyboard_action_t){.key=key_id(i),.type=2,
            .arg0=i<sizeof(mod)?mod[i]:0,.arg1=i<sizeof(usage)?usage[i]:0x04};
        actions[i][1]=actions[i][0];
    }
    actions[SYN_FN][0].type=actions[SYN_FN][1].type=0x11;
    actions[103][0].arg1=actions[103][1].arg1=0x87; /* International 1 */
    actions[SYN_TAB][1].type=0x11; actions[SYN_TAB][1].arg0=0x70;
    actions[SYN_CAPS][1].type=0x11; actions[SYN_CAPS][1].arg0=0x71;
    layout=(keyboard_layout_t){SYN_COUNT,key_id(SYN_FN),key_id(SYN_TAB),key_id(SYN_CAPS),
                              key_id(SYN_ESC),2000,levels,levels,keymap};
    small_layout=layout; small_layout.count=7;
}
const keyboard_layout_t *keyboard_layout(uint8_t p) { return p==SYN_PROFILE?&layout:p==43?&small_layout:NULL; }
uint8_t keyboard_key_for_sensor(uint8_t p, uint8_t i) { return i<keyboard_layout_count(p)?key_id(i):0; }
const keyboard_action_t *keyboard_action(uint8_t p, uint8_t key, uint8_t fn)
{
    if(!keyboard_layout(p) || !key) return NULL;
    for(unsigned i=0;i<keyboard_layout_count(p);++i) if(key_id(i)==key) return &actions[i][fn!=0];
    return NULL;
}
bool keyboard_lower_group(uint8_t p, uint8_t key)
{
    if(p!=SYN_PROFILE) return false;
    for(unsigned i=52;i<SYN_COUNT;++i) if(key_id(i)==key) return true;
    return false;
}
uint8_t keyboard_editor_digit(uint8_t p, uint8_t key)
{
    const keyboard_action_t *a=keyboard_action(p,key,0);
    return a && a->arg1>=0x1e && a->arg1<=0x27 ? a->arg1-0x1d : 0;
}
int keyboard_editor_step(uint8_t p, uint8_t key)
{
    const keyboard_action_t *a=keyboard_action(p,key,0);
    return !a?0:a->arg1==0x52?1:a->arg1==0x51?-1:0;
}
bool keyboard_editor_preview_control(uint8_t p, uint8_t key)
{
    return keyboard_editor_digit(p,key) || keyboard_editor_step(p,key) ||
           (keyboard_layout(p) && keyboard_layout(p)->fn==key);
}
void keyboard_actuation_pair(const keyboard_config_t *c,uint8_t key,uint8_t *press,uint8_t *release)
{
    (void)key;
    *press=levels[c->saved_actuation]/256u;
    *release=*press>SYNTHETIC_RELEASE_GAP_LEVEL?*press-SYNTHETIC_RELEASE_GAP_LEVEL:1;
}
uint8_t keyboard_travel_level(uint16_t lo,uint16_t hi,uint16_t raw)
{
    if(lo>=hi) return 0;
    return raw>=hi?0:raw<=lo?255:(uint32_t)(hi-raw)*255u/(hi-lo);
}
void keyboard_light_set(uint8_t p,unsigned sensor,uint8_t *frame,uint8_t r,uint8_t g,uint8_t b)
{
    if(sensor>=keyboard_layout_count(p)) return;
    frame[sensor*3]=r; frame[sensor*3+1]=g; frame[sensor*3+2]=b;
}
void keyboard_light_get(uint8_t p,unsigned sensor,const uint8_t *frame,uint8_t *r,uint8_t *g,uint8_t *b)
{
    if(sensor>=keyboard_layout_count(p)) { *r=*g=*b=0u; return; }
    *r=frame[sensor*3]; *g=frame[sensor*3+1]; *b=frame[sensor*3+2];
}
