#include "defaults.h"
#include "huntsman_layout.h"
#include "keyboard_scan.h"
#include "travel_lighting.h"
#include <stddef.h>
static const uint8_t keymap[256]={
#define KEYMAP(key,usage) [key]=usage,
#include "../config/keymap.def"
#undef KEYMAP
};

const keyboard_layout_t *keyboard_layout(uint8_t profile)
{
    static const keyboard_layout_t layouts[]={
        {61,KEY_ID_FN,KEY_ID_TAB,KEY_ID_CAPS,KEY_ID_ESC,HUNTSMAN_ASSUMED_SCAN_HZ,g_actuation_levels,g_rapid_levels,keymap,NULL},
        {62,KEY_ID_FN,KEY_ID_TAB,KEY_ID_CAPS,KEY_ID_ESC,HUNTSMAN_ASSUMED_SCAN_HZ,g_actuation_levels,g_rapid_levels,keymap,NULL},
        {65,KEY_ID_FN,KEY_ID_TAB,KEY_ID_CAPS,KEY_ID_ESC,HUNTSMAN_ASSUMED_SCAN_HZ,g_actuation_levels,g_rapid_levels,keymap,NULL}
    };
    return profile>=1 && profile<=3 ? &layouts[profile-1] : NULL;
}

bool keyboard_lower_group(uint8_t profile, uint8_t key)
{
    return keyboard_layout(profile) && key>=KEY_ID_CAPS && key<=0x39u;
}

uint8_t keyboard_editor_digit(uint8_t profile, uint8_t key)
{
    (void)profile;
    return key>=2u && key<=11u ? key-1u : 0u;
}

int keyboard_editor_step(uint8_t profile, uint8_t key)
{
    switch(key) {
        case 0x53: case 0x59: return 1;
        case 0x4f: case 0x54: return -1;
        case 0x39: case 0x40: return profile<=2u ? 1 : 0;
        case 0x3e: case 0x81: return profile<=2u ? -1 : 0;
        case 0x19: case 0x28: return profile==3u ? 1 : 0;
        case 0x26: case 0x27: return profile==3u ? -1 : 0;
        default: return 0;
    }
}

bool keyboard_editor_preview_control(uint8_t profile, uint8_t key)
{
    return keyboard_editor_digit(profile,key) || key==KEY_ID_FN ||
           keyboard_editor_step(profile,key)!=0;
}

void keyboard_actuation_pair(const keyboard_config_t *config, uint8_t key,
                             uint8_t *press, uint8_t *release)
{
    optical_key_config_t c;
    keyboard_scan_thresholds(config,key,&c);
    *press=c.press; *release=c.release;
}

uint8_t keyboard_travel_level(uint16_t lower, uint16_t upper, uint16_t raw)
{
    return optical_key_level(lower,upper,raw);
}

void keyboard_light_set(uint8_t profile, unsigned sensor, uint8_t *frame,
                        uint8_t red, uint8_t green, uint8_t blue)
{
    if(sensor>=keyboard_layout_count(profile)) return;
    const lighting_channels_t *c=&g_lighting_channels[profile-1u][sensor];
    uint8_t *p=frame+(unsigned)c->controller*192u;
    p[c->red]=red; p[c->green]=green; p[c->blue]=blue;
}
void keyboard_light_get(uint8_t profile, unsigned sensor, const uint8_t *frame,
                        uint8_t *red, uint8_t *green, uint8_t *blue)
{
    if(sensor>=keyboard_layout_count(profile)) { *red=*green=*blue=0u; return; }
    const lighting_channels_t *c=&g_lighting_channels[profile-1u][sensor];
    const uint8_t *p=frame+(unsigned)c->controller*192u;
    *red=p[c->red]; *green=p[c->green]; *blue=p[c->blue];
}
uint8_t keyboard_light_x(uint8_t profile,unsigned sensor)
{
    if(sensor>=keyboard_layout_count(profile))return 0u;
    const uint8_t key=keyboard_key_for_sensor(profile,sensor);
    /* The production physical IDs follow the ANSI row sequence even though
     * the ASIC scan order and LED channels do not. Optional ISO/JIS keys use
     * a nearby column when they lack a corresponding ANSI-row position. */
    if(key>=1u && key<=0x0fu)return (key-1u)*4u;
    if(key>=0x10u && key<=0x1du)return (key-0x10u)*4u+1u;
    if(key>=0x1eu && key<=0x2bu)return (key-0x1eu)*4u+2u;
    if(key>=0x2cu && key<=0x39u)return (key-0x2cu)*4u+3u;
    if(key>=0x3au && key<=0x40u)return (key-0x3au)*9u+2u;
    if(key==KEY_ID_ESC)return 0u;
    return 56u; /* supplemental layout keys at the right edge */
}
