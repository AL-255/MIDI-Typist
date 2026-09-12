#include "keyboard_app.h"
#include <string.h>

static bool decimal(const char **text,uint32_t *value)
{
    const char *p=*text;
    if(*p<'0' || *p>'9') return false;
    *value=0;
    while(*p>='0' && *p<='9') {
        unsigned digit=*p++-'0';
        if(*value>(UINT32_MAX-digit)/10u) return false;
        *value=*value*10u+digit;
    }
    *text=p; return true;
}
static bool argument(const char **p,uint32_t *v)
{
    if(**p!=' ') return false;
    ++*p; return decimal(p,v);
}
bool keyboard_app_command(keyboard_app_t *s,const char *line,uint32_t now,
                          bool healthy,uint32_t *ack,uint8_t *result)
{
    if(!line || strncmp(line,"cfg ",4)) return false;
    const char *p=strchr(line+4,' ');
    uint32_t id=0,a=0,b=0,c=0;
    if(!p || !argument(&p,&id) || !id) return true;
    *ack=id; *result=2u;
    if(!strncmp(line,"cfg get ",8) && !*p) *result=1u;
    else if(!strncmp(line,"cfg calcancel ",14) && !*p) {
        calibration_abort(s->cal,CAL_CANCELLED,now);
        keyboard_raw_invalidate(s->raw); *result=1u;
    } else if(!strncmp(line,"cfg calibrate ",14) && !*p) {
        if(keyboard_app_calibrate(s,now,healthy)) *result=1u;
    } else if(calibration_active(s->cal)) return true;
    else if(s->raw->engine.config.mode && strncmp(line,"cfg enable ",11)) return true;
    else if(!strncmp(line,"cfg enable ",11) && argument(&p,&a) && !*p && a<=1u) {
        keyboard_raw_enable(s->raw,a!=0); *result=1u;
    } else if(!strncmp(line,"cfg set ",8) && argument(&p,&a) && argument(&p,&b) &&
              argument(&p,&c) && !*p && keyboard_raw_set(s->raw,a,b,c)) *result=1u;
    else if(!strncmp(line,"cfg all ",8) && argument(&p,&a) && argument(&p,&b) &&
            !*p && keyboard_raw_set_all(s->raw,a,b)) *result=1u;
    else if(!strncmp(line,"cfg midi ",9) && argument(&p,&a) && argument(&p,&b) &&
            !*p && keyboard_midi_map(s->midi,s->raw,a,b)) *result=1u;
    else if(!strncmp(line,"cfg velocity ",13) && argument(&p,&a) && !*p && a>=1u && a<=10u) {
        keyboard_midi_set_velocity_start(s->midi,a); *result=1u;
    }
    return true;
}
