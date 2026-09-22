#include "m1_radio.h"
#include "defaults.h"
#include <string.h>

bool m1_radio_encode(m1_radio_packet_t *out,uint8_t opcode,const uint8_t *payload,size_t length)
{
    if(!out || !payload || !length || length>M1_RADIO_PAYLOAD_MAX)return false;
    switch(opcode) {
    case M1_RADIO_REPORT: case M1_RADIO_BATTERY: case M1_RADIO_STATUS_REQUEST:
    case M1_RADIO_MODE: case M1_RADIO_CONTROL: break;
    default: return false;
    }
    m1_radio_packet_t packet={0};
    packet.bytes[0]=opcode; packet.bytes[1]=length;
    memcpy(packet.bytes+2,payload,length);
    uint8_t sum=0;
    for(size_t i=0;i<length;++i)sum+=payload[i];
    packet.bytes[length+2u]=sum;
    packet.size=(length+6u)&~3u; /* round (header + payload + checksum) up to 4 */
    *out=packet;
    return true;
}
bool m1_radio_make_pair(m1_radio_packet_t *out,unsigned mode)
{
    /* 0x0801819a: payload length 33, name length 12, slot digit at packet
     * byte 15. This is framing, not proof of pairing or a radio-host ACK. */
    static const char name[]=M1_BT_PAIR_NAME;
    _Static_assert(sizeof(name)==12,"M1 pairing prefix must contain 11 bytes");
    if(mode==5) {
        const uint8_t payload[]={0,1};
        return m1_radio_encode(out,M1_RADIO_CONTROL,payload,sizeof(payload));
    }
    if(mode>2)return false;
    uint8_t payload[33]={2,12};
    memcpy(payload+2,name,sizeof(name)-1u);payload[13]='1'+mode;
    return m1_radio_encode(out,M1_RADIO_CONTROL,payload,sizeof(payload));
}
void m1_radio_make_poll(m1_radio_packet_t *out)
{
    if(!out)return;
    *out=(m1_radio_packet_t){.bytes={M1_RADIO_POLL},.size=M1_RADIO_POLL_BYTES};
}
bool m1_radio_decode(const uint8_t *bytes,size_t transferred,m1_radio_reply_t *out)
{
    if(!bytes || !out || transferred<4u || transferred>M1_RADIO_BUFFER_BYTES)return false;
    size_t count=bytes[1];
    if(!count || count>M1_RADIO_PAYLOAD_MAX || count+3u>transferred)return false;
    uint8_t sum=0;
    for(size_t i=0;i<count;++i)sum+=bytes[i+2u];
    if(sum!=bytes[count+2u])return false;
    m1_radio_reply_t reply={.kind=bytes[2],.length=count-1u};
    memcpy(reply.data,bytes+3,count-1u);
    *out=reply;
    return true;
}
bool m1_radio_status(const m1_radio_reply_t *reply,m1_radio_status_t *out)
{
    if(!reply || !out || reply->kind!=M1_RADIO_REPLY_STATUS ||
       reply->length<3u || reply->length>sizeof(reply->data))return false;
    *out=(m1_radio_status_t){reply->data[0],reply->data[1],reply->data[2]};
    return true;
}
