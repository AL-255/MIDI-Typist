#include "m1_radio.h"
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
