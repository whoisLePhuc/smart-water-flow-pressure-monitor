#include "storage_codec.h"
#include <string.h>

#define MAGIC      0x53574650u
#define REC_VOLUME 3u
#define SCHEMA_V1  1u
#define HDR_SIZE   16u

static uint32_t crc32_partial(uint32_t crc, const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320u : 0);
    }
    return crc;
}

uint32_t storage_codec_crc32(const uint8_t *data, uint16_t len)
{
    return crc32_partial(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

uint16_t storage_codec_encode_volume(uint8_t *buf, const VolumeStoragePayload *p)
{
    if (!buf || !p) return 0;
    memset(buf, 0, SLOT_VOLUME_SIZE);

    buf[0]=0x50; buf[1]=0x46; buf[2]=0x57; buf[3]=0x53;
    buf[4]=REC_VOLUME; buf[5]=SCHEMA_V1;
    uint16_t psz=20u;
    buf[6]=(uint8_t)psz; buf[7]=(uint8_t)(psz>>8);
    buf[8]=(uint8_t)p->sequence; buf[9]=(uint8_t)(p->sequence>>8);
    buf[10]=(uint8_t)(p->sequence>>16); buf[11]=(uint8_t)(p->sequence>>24);

    uint8_t *pay = buf + HDR_SIZE;
    uint64_t fw=p->forward_volume_ul, rv=p->reverse_volume_ul;
    for(int i=0;i<8;i++){ pay[i]=(uint8_t)(fw>>(i*8)); pay[8+i]=(uint8_t)(rv>>(i*8)); }

    uint32_t crc = crc32_partial(crc32_partial(0xFFFFFFFFu, buf, 12), buf+HDR_SIZE, psz);
    buf[12]=(uint8_t)crc; buf[13]=(uint8_t)(crc>>8);
    buf[14]=(uint8_t)(crc>>16); buf[15]=(uint8_t)(crc>>24);

    buf[SLOT_VOLUME_SIZE-1] = 0xA5u;
    return SLOT_VOLUME_SIZE;
}

bool storage_codec_decode_volume(const uint8_t *buf, VolumeStoragePayload *p)
{
    if (!buf || !p) return false;
    if (buf[SLOT_VOLUME_SIZE-1]!=0xA5u) return false;
    if (buf[4]!=REC_VOLUME) return false;

    uint32_t crc = crc32_partial(crc32_partial(0xFFFFFFFFu, buf, 12), buf+HDR_SIZE, 20u);
    uint32_t exp = (uint32_t)buf[12]|((uint32_t)buf[13]<<8)|((uint32_t)buf[14]<<16)|((uint32_t)buf[15]<<24);
    if (crc != exp) return false;

    memset(p,0,sizeof(*p));
    const uint8_t *pay=buf+HDR_SIZE;
    for(int i=0;i<8;i++){ p->forward_volume_ul |= ((uint64_t)pay[i]<<(i*8)); }
    for(int i=0;i<8;i++){ p->reverse_volume_ul |= ((uint64_t)pay[8+i]<<(i*8)); }
    p->sequence = (uint32_t)buf[8]|((uint32_t)buf[9]<<8)|((uint32_t)buf[10]<<16)|((uint32_t)buf[11]<<24);
    return true;
}
