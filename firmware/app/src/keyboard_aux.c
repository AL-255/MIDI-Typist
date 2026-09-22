#include "keyboard_aux.h"
#include <string.h>
_Static_assert(AUX_PULSE_MS>0 && AUX_PULSE_MS<1000,"auxiliary pulse duration");
void keyboard_aux_init(keyboard_aux_t *s,const uint16_t mapping[3])
{ *s=(keyboard_aux_t){.release=true};memcpy(s->mapping,mapping,sizeof(s->mapping)); }
void keyboard_aux_cancel(keyboard_aux_t *s)
{ s->pending=0;s->release=true;s->allowed=false; }
bool keyboard_aux_idle(const keyboard_aux_t *s)
{ return !s->release && !s->usage && !s->pending; }
bool keyboard_aux_offer(keyboard_aux_t *s,uint8_t events)
{
    if(!s->allowed || !keyboard_aux_idle(s) || (events&~15u))return false;
    s->pending=events&7u;return true;
}
void keyboard_aux_service(keyboard_aux_t *s,bool allowed,uint32_t now,bool (*send)(uint16_t))
{
    if(!allowed && s->allowed)keyboard_aux_cancel(s);
    s->allowed=allowed;
    if(s->usage && (uint32_t)(now-s->pressed_at)>=AUX_PULSE_MS)s->release=true;
    if(s->release) {
        if(send && send(0)) { s->usage=0;s->release=false; }
        return;
    }
    if(!allowed || s->usage || !s->pending || !send)return;
    unsigned index=0;while(!(s->pending&(1u<<index)))++index;
    uint16_t usage=s->mapping[index];
    if(!usage || send(usage)) {
        s->pending&=~(1u<<index);s->usage=usage;s->pressed_at=now;
    }
}
