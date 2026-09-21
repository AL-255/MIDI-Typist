#include "fun60_board.h"
#include "defaults.h"
#include "keyboard_layout.h"
#include "keyboard_lighting.h"
#include <stddef.h>

static const uint16_t actuation[11]=DEFAULT_ACTUATION_LEVELS;
static const uint16_t rapid[11]=DEFAULT_RAPID_LEVELS;
static const keyboard_action_t actions[FUN60_KEY_COUNT]={
#define FUN60_KEY(index,label,usage,row,channel,led,x,y,width) \
    [index]={.key=(index)+1, .type=(usage)?2:0x11, \
             .arg0=(usage)>=0xe0 ? (1u<<((usage)&7u)) : 0, \
             .arg1=(usage)<0xe0 ? (usage) : 0},
#include "fun60_keys.def"
#undef FUN60_KEY
};

const keyboard_layout_t *keyboard_layout(uint8_t profile)
{
    static const keyboard_layout_t layout={FUN60_KEY_COUNT,FUN60_KEY_FN,FUN60_KEY_TAB,FUN60_KEY_CAPS,FUN60_KEY_ESCAPE,
        FUN60_SCAN_HZ,actuation,rapid,true};
    return profile==FUN60_PROFILE ? &layout : NULL;
}
uint8_t keyboard_key_for_sensor(uint8_t profile,uint8_t sensor)
{
    return profile==FUN60_PROFILE && sensor<FUN60_KEY_COUNT ? sensor+1u : 0;
}
const keyboard_action_t *keyboard_action(uint8_t profile,uint8_t key,uint8_t fn)
{
    (void)fn; /* shared application owns its Fn shortcuts and menus */
    return profile==FUN60_PROFILE && key && key<=FUN60_KEY_COUNT ? &actions[key-1u] : NULL;
}
bool keyboard_lower_group(uint8_t profile,uint8_t key)
{
    return keyboard_action(profile,key,0) && fun60_keys[key-1u].y>=2u && fun60_keys[key-1u].y<=3u;
}
uint8_t keyboard_editor_digit(uint8_t profile,uint8_t key)
{
    const keyboard_action_t *a=keyboard_action(profile,key,0);
    return a && a->arg1>=0x1e && a->arg1<=0x27 ? a->arg1-0x1d : 0;
}
int keyboard_editor_step(uint8_t profile,uint8_t key)
{
    const keyboard_action_t *a=keyboard_action(profile,key,0);
    if (!a) return 0;
    return a->arg0==0x20u ? 1 : a->arg1==0x65u ? -1 : 0; /* RShift / Menu */
}
bool keyboard_editor_preview_control(uint8_t profile,uint8_t key)
{
    const keyboard_layout_t *layout=keyboard_layout(profile);
    return layout && (key==layout->fn || keyboard_editor_digit(profile,key) || keyboard_editor_step(profile,key));
}
void keyboard_actuation_pair(const keyboard_config_t *config,uint8_t key,uint8_t *press,uint8_t *release)
{
    (void)key;
    unsigned level=config->actuation;
    if (level<1u || level>10u) level=DEFAULT_ACTUATION_LEVEL;
    *press=actuation[level]>>8;
    *release=*press>FUN60_RELEASE_GAP_LEVEL ? *press-FUN60_RELEASE_GAP_LEVEL : 1u;
}
uint8_t keyboard_travel_level(uint16_t lo,uint16_t hi,uint16_t raw)
{
    if (lo>=hi || raw>=hi) return 0;
    return raw<=lo ? 255u : (uint32_t)(hi-raw)*255u/(hi-lo);
}
void keyboard_light_set(uint8_t profile,unsigned sensor,uint8_t *frame,uint8_t r,uint8_t g,uint8_t b)
{
    if (sensor>=keyboard_layout_count(profile)) return;
    frame[3u*sensor]=r;frame[3u*sensor+1u]=g;frame[3u*sensor+2u]=b;
}
void keyboard_light_get(uint8_t profile,unsigned sensor,const uint8_t *frame,uint8_t *r,uint8_t *g,uint8_t *b)
{
    *r=*g=*b=0;
    if (sensor>=keyboard_layout_count(profile)) return;
    *r=frame[3u*sensor];*g=frame[3u*sensor+1u];*b=frame[3u*sensor+2u];
}
