#include "midi_sysex.h"
#include <string.h>

static const uint8_t prefix[]={0xf0,0x7d,0x4d,0x54,MT_SYSEX_VERSION};
static uint32_t crc_update(uint32_t crc,const uint8_t *p,size_t n)
{
    /* Reflected IEEE CRC-32, four polynomial steps per lookup. The small
     * table keeps the wire CRC unchanged without an eight-step bit loop for
     * every captured byte. No MCU-specific CRC peripheral or alignment. */
    static const uint32_t nibble[16]={
        0x00000000u,0x1db71064u,0x3b6e20c8u,0x26d930acu,
        0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,
        0xedb88320u,0xf00f9344u,0xd6d6a3e8u,0xcb61b38cu,
        0x9b64c2b0u,0x86d3d2d4u,0xa00ae278u,0xbdbdf21cu
    };
    while(n--) {
        crc^=*p++;
        crc=(crc>>4)^nibble[crc&15u];
        crc=(crc>>4)^nibble[crc&15u];
    }
    return crc;
}
static uint32_t u32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i) p[i]=v>>(8*i); }
size_t midi_sysex_encode(uint8_t kind,uint32_t session,uint32_t sequence,
                        const uint8_t *payload,size_t length,uint8_t *out,size_t capacity)
{
    if(!out || !session || kind<MT_HELLO || kind>MT_CLOSE || length>MT_SYSEX_MAX_PAYLOAD ||
       (length && !payload) || capacity<MT_SYSEX_WIRE_SIZE(length)) return 0;
    uint8_t raw[MT_SYSEX_MAX_PAYLOAD+14u];
    put32(raw,session);put32(raw+4,sequence);raw[8]=length;raw[9]=length>>8;
    if(length) memcpy(raw+10,payload,length);
    memcpy(out,prefix,sizeof(prefix));out[5]=kind;
    put32(raw+10+length,crc_update(crc_update(~0u,out+1,5),raw,10+length)^~0u);
    size_t used=6;
    for(size_t offset=0;offset<length+14u;) {
        size_t count=length+14u-offset;if(count>7)count=7;
        size_t mask=used++;out[mask]=0;
        for(size_t i=0;i<count;++i) {
            uint8_t value=raw[offset++];out[mask]|=(value>>7)<<i;out[used++]=value&127u;
        }
    }
    out[used++]=0xf7;return used;
}
bool midi_sysex_decode(const uint8_t *wire,size_t length,midi_sysex_info_t *info,
                       uint8_t *payload,size_t capacity)
{
    if(!wire || !info || length<MT_SYSEX_WIRE_SIZE(0) || length>MT_SYSEX_MAX_WIRE ||
       memcmp(wire,prefix,sizeof(prefix)) || wire[length-1]!=0xf7 ||
       wire[5]<MT_HELLO || wire[5]>MT_CLOSE) return false;
    uint8_t raw[MT_SYSEX_MAX_PAYLOAD+14u];size_t used=0;
    for(size_t at=6;at<length-1;) {
        uint8_t mask=wire[at++];size_t count=length-1-at;if(count>7)count=7;
        if(!count || mask>>count || used+count>sizeof(raw)) return false;
        for(size_t i=0;i<count;++i) {
            if(wire[at]&128u) return false;
            raw[used++]=wire[at++]|((mask>>i)&1u)<<7;
        }
    }
    if(used<14) return false;
    size_t size=raw[8]|(size_t)raw[9]<<8;
    if(size+14u!=used || size>capacity || (size && !payload) || !u32(raw) ||
       u32(raw+10+size)!=(crc_update(crc_update(~0u,wire+1,5),raw,10+size)^~0u)) return false;
    *info=(midi_sysex_info_t){u32(raw),u32(raw+4),(uint16_t)size,wire[5]};
    if(size)memcpy(payload,raw+10,size);
    return true;
}
