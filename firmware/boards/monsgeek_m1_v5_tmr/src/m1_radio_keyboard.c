#include "m1_radio_keyboard.h"
#include <string.h>

static bool in_slots(const m1_radio_keyboard_t *s,unsigned usage)
{
    for(unsigned i=0;i<M1_RADIO_KEY_SLOTS;++i)
        if(s->slots[i]==usage)return true;
    return false;
}
bool m1_radio_keyboard_update(m1_radio_keyboard_t *s,const keyboard_report_t *report)
{
    if(!s || !report || report->reserved)return false;
    m1_radio_keyboard_t next={.modifiers=report->modifiers};
    unsigned count=0;
    /* Do not move a held key between the two independently delivered reports. */
    for(unsigned i=0;i<M1_RADIO_KEY_SLOTS;++i)
        if(s->slots[i]>=KEYBOARD_NKRO_USAGE_MIN &&
           keyboard_report_get_usage(report,s->slots[i]))next.slots[count++]=s->slots[i];
    for(unsigned usage=KEYBOARD_NKRO_USAGE_MIN;usage<=M1_RADIO_KEY_BITMAP_MAX;++usage)
        if((s->bitmap[usage/8u]&(1u<<(usage%8u))) && keyboard_report_get_usage(report,usage))
            next.bitmap[usage/8u]|=1u<<(usage%8u);
    /* New extended usages have no bitmap representation: give them free slots
     * first. Existing held slots are never displaced to manufacture room. */
    for(unsigned pass=0;pass<2;++pass) {
        unsigned first=pass?KEYBOARD_NKRO_USAGE_MIN:M1_RADIO_KEY_BITMAP_MAX+1u;
        unsigned last=pass?M1_RADIO_KEY_BITMAP_MAX:KEYBOARD_NKRO_USAGE_MAX;
        for(unsigned usage=first;usage<=last;++usage) {
            if(!keyboard_report_get_usage(report,usage) || in_slots(&next,usage))continue;
            bool low=usage<=M1_RADIO_KEY_BITMAP_MAX;
            if(low && (next.bitmap[usage/8u]&(1u<<(usage%8u))))continue;
            if(count<M1_RADIO_KEY_SLOTS)next.slots[count++]=usage;
            else if(low)next.bitmap[usage/8u]|=1u<<(usage%8u);
            else next.rollover=true;
        }
    }
    *s=next;return true;
}
bool m1_radio_keyboard_packet(const m1_radio_keyboard_t *s,unsigned subtype,m1_radio_packet_t *out)
{
    if(!s || !out || (subtype!=M1_RADIO_KEY_LIST && subtype!=M1_RADIO_KEY_BITMAP))return false;
    uint8_t payload[1u+M1_RADIO_KEY_BITMAP_BYTES]={0};payload[0]=subtype;
    if(subtype==M1_RADIO_KEY_LIST) {
        payload[1]=s->modifiers;
        if(s->rollover)memset(payload+2,M1_RADIO_KEY_ROLLOVER,M1_RADIO_KEY_SLOTS);
        else memcpy(payload+2,s->slots,M1_RADIO_KEY_SLOTS);
        return m1_radio_encode(out,M1_RADIO_REPORT,payload,2u+M1_RADIO_KEY_SLOTS);
    }
    memcpy(payload+1,s->bitmap,sizeof(s->bitmap));
    return m1_radio_encode(out,M1_RADIO_REPORT,payload,sizeof(payload));
}
