#include "defaults.h"
#include "keyboard_layout.h"
#include "keyboard_lighting.h"
#include <string.h>
_Static_assert(RAINBOW_CYCLE_MS && RAINBOW_LIGHT_X_SPAN &&
               RAINBOW_HUE_PERIOD==6u*RAINBOW_HUE_SEGMENT,
               "invalid rainbow effect geometry");

uint8_t lighting_travel_pwm(uint16_t raw, uint16_t lower, uint16_t upper)
{
    if (!raw || raw > 4096u || !lower || lower >= upper || upper > 4096u) return 0u;
    if (raw <= lower) return 255u;
    if (raw >= upper) return 0u;
    return ((uint32_t)(upper - raw) * 255u + (upper - lower) / 2u) / (upper - lower);
}

void lighting_travel_frame(uint8_t profile, const uint16_t *raw, const uint16_t *lower,
                          const uint16_t *upper, bool valid, uint8_t *frame)
{
    memset(frame, 0, LIGHTING_FRAME_SIZE);
    if (!valid || !keyboard_layout_count(profile)) return;
    const unsigned count = keyboard_layout_count(profile);
    for (unsigned i = 0; i < count; ++i)
    {
        /* Invert only valid optical travel; invalid data must remain dark.
         * The shared travel helper stays press-increasing for MIDI pressure. */
        const bool usable=raw[i] && raw[i]<=4096u && lower[i] &&
                          lower[i]<upper[i] && upper[i]<=4096u;
        const uint8_t pwm = usable ? 255u-lighting_travel_pwm(raw[i],lower[i],upper[i]) : 0u;
        keyboard_light_set(profile,i,frame,pwm,pwm,pwm);
    }
}

/* Six linear RGB segments form a full color wheel. The reference firmware
 * renders effects across five physical rows of fifteen RGB cells; this
 * independent effect uses board-owned x positions rather than ASIC order. */
void lighting_rainbow_frame(uint8_t profile, unsigned count, uint8_t *frame, uint32_t now)
{
    const uint32_t time_phase=(now%RAINBOW_CYCLE_MS)*RAINBOW_HUE_PERIOD/RAINBOW_CYCLE_MS;
    for(unsigned i=0;i<count;++i) {
        uint8_t level,unused1,unused2;
        keyboard_light_get(profile,i,frame,&level,&unused1,&unused2);
        uint32_t x=keyboard_light_x(profile,i);
        if(x>RAINBOW_LIGHT_X_SPAN)x=RAINBOW_LIGHT_X_SPAN;
        const uint32_t hue=(time_phase+x*RAINBOW_HUE_PERIOD/RAINBOW_LIGHT_X_SPAN)%RAINBOW_HUE_PERIOD;
        const uint8_t step=(uint8_t)(hue%RAINBOW_HUE_SEGMENT);
        const uint8_t falling=255u-step;
        uint8_t red=0,green=0,blue=0;
        switch(hue/RAINBOW_HUE_SEGMENT) {
            case 0: red=255u;green=step;break;
            case 1: red=falling;green=255u;break;
            case 2: green=255u;blue=step;break;
            case 3: green=falling;blue=255u;break;
            case 4: red=step;blue=255u;break;
            default: red=255u;blue=falling;break;
        }
        keyboard_light_set(profile,i,frame,
            ((uint16_t)red*level+127u)/255u,
            ((uint16_t)green*level+127u)/255u,
            ((uint16_t)blue*level+127u)/255u);
    }
}
