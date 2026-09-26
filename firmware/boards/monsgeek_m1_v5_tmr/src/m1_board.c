#include "m1_board.h"
#include "defaults.h"
#include "keyboard_layout.h"
#include "keyboard_lighting.h"
#include <stddef.h>
#include <string.h>

const m1_key_t m1_keys[M1_KEY_COUNT] = {
#define M1_KEY(id,bank,rank,usage,x,y,w,label) [id]={bank,rank,usage},
#include "m1_keys.def"
#undef M1_KEY
};
static const uint8_t light_x[M1_KEY_COUNT] = {
#define M1_KEY(id,bank,rank,usage,x,y,w,label) [id]=(x)+(w)/2u,
#include "m1_keys.def"
#undef M1_KEY
};
const uint8_t m1_adc_channels[M1_ADC_RANKS]={10,11,12,13,0,1,2,3,4,5,6,7,14,15,8};
/* Verified by executing the reference bank selector, not by reading paired
 * hex halfwords as bytes: its TBB entries are byte-indexed. Bits are B9/B8/B7. */
const uint8_t m1_bank_bits[M1_BANK_COUNT]={6,0,4,2,1,3};
static const keyboard_action_t actions[M1_KEY_COUNT] = {
#define M1_KEY(id,bank,rank,usage,x,y,w,label) \
    [id]={.key=(id)+1u,.type=(usage)?2:0x11,.length=8, \
          .arg0=(usage)>=0xe0?(1u<<((usage)-0xe0)):0,.arg1=(usage)<0xe0?(usage):0},
#include "m1_keys.def"
#undef M1_KEY
};
static const uint16_t levels[11]=M1_ACTUATION_LEVELS;
static const uint8_t keymap[256]={
#define KEYMAP(key,usage) [key]=usage,
#include "../config/keymap.def"
#undef KEYMAP
};
static const keyboard_input_policy_t input_policy={true,M1_CALIBRATION_MIN_SPAN_RAW,
    M1_CALIBRATION_MIN_RELEASE_RAW,M1_CALIBRATION_PRESS_DROP_RAW};
static const keyboard_layout_t layout={M1_KEY_COUNT,M1_FN_SENSOR+1u,
    M1_TAB_SENSOR+1u,M1_CAPS_SENSOR+1u,M1_ESC_SENSOR+1u,M1_SCAN_HZ,levels,levels,keymap,&input_policy};

const keyboard_layout_t *keyboard_layout(uint8_t profile)
{ return profile==M1_PROFILE?&layout:NULL; }
uint8_t keyboard_key_for_sensor(uint8_t profile,uint8_t sensor)
{ return profile==M1_PROFILE && sensor<M1_KEY_COUNT?sensor+1u:0; }
const keyboard_action_t *keyboard_action(uint8_t profile,uint8_t key,uint8_t fn)
{
    static const keyboard_action_t trigger={M1_TAB_SENSOR+1u,0x11,8,0x70,0,0,0,0};
    static const keyboard_action_t rapid={M1_CAPS_SENSOR+1u,0x11,8,0x71,0,0,0,0};
    if(profile!=M1_PROFILE || !key || key>M1_KEY_COUNT) return NULL;
    if(fn && key==M1_TAB_SENSOR+1u) return &trigger;
    if(fn && key==M1_CAPS_SENSOR+1u) return &rapid;
    return &actions[key-1u];
}
bool keyboard_lower_group(uint8_t profile,uint8_t key)
{
    return profile==M1_PROFILE && key && key<=M1_KEY_COUNT &&
           (m1_keys[key-1u].bank==3u || m1_keys[key-1u].bank==4u);
}
uint8_t keyboard_editor_digit(uint8_t profile,uint8_t key)
{
    const keyboard_action_t *a=keyboard_action(profile,key,0);
    return a && a->arg1>=0x1e && a->arg1<=0x27?a->arg1-0x1d:0;
}
int keyboard_editor_step(uint8_t profile,uint8_t key)
{
    const keyboard_action_t *a=keyboard_action(profile,key,0);
    return !a?0:a->arg1==0x52?1:a->arg1==0x51?-1:0;
}
bool keyboard_editor_preview_control(uint8_t profile,uint8_t key)
{
    return keyboard_editor_digit(profile,key) || keyboard_editor_step(profile,key) ||
           (profile==M1_PROFILE && key==M1_FN_SENSOR+1u);
}
void keyboard_actuation_pair(const keyboard_config_t *c,uint8_t key,uint8_t *press,uint8_t *release)
{
    (void)key;
    unsigned level=c->saved_actuation>=1u && c->saved_actuation<=10u?
                   c->saved_actuation:DEFAULT_ACTUATION_LEVEL;
    *press=levels[level]/256u;
    *release=*press>M1_RELEASE_GAP_LEVEL?*press-M1_RELEASE_GAP_LEVEL:1u;
}
uint8_t keyboard_travel_level(uint16_t lo,uint16_t hi,uint16_t raw)
{
    if(lo>=hi) return 0;
    return raw>=hi?0:raw<=lo?255:(uint32_t)(hi-raw)*255u/(hi-lo);
}
uint8_t m1_led_index(unsigned sensor)
{
    if(sensor>=M1_KEY_COUNT) return UINT8_MAX;
    unsigned bank=m1_keys[sensor].bank;
    /* Physical chain snakes left/right per row. Sums are first+last
     * compact sensor ID in each reversed row. */
    static const uint8_t reverse_sum[M1_BANK_COUNT]={0,42,0,101,0,153};
    return bank&1u?reverse_sum[bank]-sensor:sensor;
}
unsigned m1_factory_cell(unsigned sensor)
{ return sensor<M1_KEY_COUNT?m1_keys[sensor].rank*M1_BANK_COUNT+m1_keys[sensor].bank:UINT32_MAX; }
void keyboard_light_set(uint8_t profile,unsigned sensor,uint8_t *frame,uint8_t r,uint8_t g,uint8_t b)
{
    if(profile!=M1_PROFILE || sensor>=M1_KEY_COUNT) return;
    unsigned i=m1_led_index(sensor)*3u;
    frame[i]=r; frame[i+1]=g; frame[i+2]=b;
}
void keyboard_light_get(uint8_t profile,unsigned sensor,const uint8_t *frame,uint8_t *r,uint8_t *g,uint8_t *b)
{
    if(profile!=M1_PROFILE || sensor>=M1_KEY_COUNT) { *r=*g=*b=0; return; }
    unsigned i=m1_led_index(sensor)*3u;
    *r=frame[i]; *g=frame[i+1]; *b=frame[i+2];
}
void m1_light_test_bottom(uint8_t frame[M1_LED_BYTES])
{
    /* Physical order: LCtrl LWin LAlt Space RAlt Fn RCtrl Left Down Right.
     * Up sits directly above Down; adjacent lit keys have distinct colors. */
    static const struct { uint8_t sensor,r,g,b; } pattern[]={
        {72,255,255,255}, {73,255,0,0}, {74,0,255,0}, {75,0,0,255},
        {76,255,255,255}, {77,255,0,0}, {78,0,255,0},
        {79,0,0,255}, {80,255,255,255}, {81,255,0,0},
        {70,0,255,0},
    };
    memset(frame,0,M1_LED_BYTES);
    for(unsigned i=0;i<sizeof(pattern)/sizeof(pattern[0]);++i)
        keyboard_light_set(M1_PROFILE,pattern[i].sensor,frame,
                           pattern[i].r,pattern[i].g,pattern[i].b);
}
uint8_t keyboard_light_x(uint8_t profile,unsigned sensor)
{ return profile==M1_PROFILE && sensor<M1_KEY_COUNT?light_x[sensor]:0u; }
