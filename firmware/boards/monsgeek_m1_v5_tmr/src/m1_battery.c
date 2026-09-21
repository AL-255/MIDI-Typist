#include "m1_battery.h"
#include "keyboard_lighting.h"
#include <string.h>

void m1_battery_init(m1_battery_t *s) { memset(s,0,sizeof(*s)); }
void m1_battery_invalidate(m1_battery_t *s) { m1_battery_init(s); }
uint8_t m1_battery_percent(uint16_t adc)
{
    if(adc<=M1_BATTERY_ADC_EMPTY)return 1;
    if(adc>=M1_BATTERY_ADC_FULL)return 100;
    if(adc<=M1_BATTERY_ADC_KNEE)
        return 20u-(M1_BATTERY_ADC_KNEE-adc)*20u/
                    (M1_BATTERY_ADC_KNEE-M1_BATTERY_ADC_EMPTY);
    return 20u+(adc-M1_BATTERY_ADC_KNEE)*80u/
               (M1_BATTERY_ADC_FULL-M1_BATTERY_ADC_KNEE);
}
bool m1_battery_sample(m1_battery_t *s,uint16_t adc,bool pc13_high,
                       bool pb10_high,uint32_t now)
{
    if(adc>M1_ADC_MAX) { m1_battery_invalidate(s); return false; }
    bool external=!pc13_high;
    if(!s->source_known || external!=s->externally_powered) {
        /* Do not mix charge/discharge batches or inherit a monotonicity gate
         * across cable changes. Old status is not a valid new measurement. */
        m1_battery_init(s);
        s->source_known=true; s->externally_powered=external;
    }
    if(s->sample_clock && (uint32_t)(now-s->sampled_at)<M1_BATTERY_SAMPLE_MS)return false;
    s->sample_clock=true; s->sampled_at=now;
    m1_charger_status_t charger=!external?M1_CHARGER_ON_BATTERY:
        pb10_high?M1_CHARGER_PIN_HIGH:M1_CHARGER_PIN_LOW;
    if(charger!=s->charger_candidate) { s->charger_candidate=charger; s->charger_count=0; }
    if(s->charger_count<M1_CHARGER_CONFIRM_SAMPLES)++s->charger_count;
    if(s->charger_count==M1_CHARGER_CONFIRM_SAMPLES)s->charger=charger;

    s->samples[s->count++]=adc;
    if(s->count<M1_BATTERY_FILTER_SAMPLES)return false;
    uint32_t sum=0;
    for(unsigned i=0;i<M1_BATTERY_FILTER_SAMPLES;++i)sum+=s->samples[i];
    uint16_t average=sum/M1_BATTERY_FILTER_SAMPLES;
    s->count=0;
    s->average=s->valid?(s->average+average)/2u:average;
    uint8_t percent=m1_battery_percent(s->average);
    /* Reference caps the displayed level at 99 for PB10 high on USB power.
     * Deliberately do not turn an empty/absent battery into a fake 100%. */
    if(external && pb10_high) {
        if(percent==100)percent=99;
        if(s->valid && s->percent==100)s->percent=99;
    }
    if(!s->valid) {
        s->percent=percent; s->valid=true; return true;
    }
    bool progress=external?percent>s->percent:percent<s->percent;
    if(!progress) { s->confirmations=0; return true; }
    if(s->candidate!=percent) { s->candidate=percent; s->confirmations=0; }
    if(++s->confirmations>=M1_BATTERY_CONFIRM_BATCHES) {
        s->percent=percent; s->confirmations=0;
    }
    return true;
}
bool m1_battery_low(const m1_battery_t *s)
{ return s->valid && !s->externally_powered && s->percent<=M1_BATTERY_LOW_PERCENT; }
bool m1_battery_critical(const m1_battery_t *s)
{ return s->valid && !s->externally_powered && s->percent<=M1_BATTERY_CRITICAL_PERCENT; }
void m1_battery_lights(const m1_battery_t *s,bool requested,uint8_t frame[M1_LED_BYTES],uint32_t now)
{
    bool low=m1_battery_low(s);
    if(!requested && !low)return;
    memset(frame,0,M1_LED_BYTES);
    if(requested) {
        if(!s->valid) {
            keyboard_light_set(M1_PROFILE,M1_SPACE_SENSOR,frame,COLOR_RAPID);
            return;
        }
        unsigned bars=(s->percent+9u)/10u;
        for(unsigned digit=0;digit<bars;++digit)
            keyboard_light_set(M1_PROFILE,M1_DIGIT1_SENSOR+digit,frame,
                low?M1_BATTERY_DISPLAY_PWM:0,low?0:M1_BATTERY_DISPLAY_PWM,0);
    }
    /* Physical LED78 is logical left Alt (sensor74) on this serpentine row. */
    if(low && ((now/M1_BATTERY_BLINK_MS)&1u))
        keyboard_light_set(M1_PROFILE,M1_LEFT_ALT_SENSOR,frame,M1_BATTERY_DISPLAY_PWM,0,0);
}
