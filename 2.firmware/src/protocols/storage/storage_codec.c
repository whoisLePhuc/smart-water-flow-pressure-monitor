#include "storage_codec.h"
#include <string.h>

uint16_t storage_codec_encode_volume(uint8_t *slot_buffer, const VolumeStoragePayload *payload)
{
    if (!slot_buffer || !payload) return 0;
    uint8_t buf[SLOT_VOLUME_SIZE];
    memset(buf, 0, sizeof(buf));
    uint32_t seq_le = payload->sequence;
    uint32_t crc = storage_codec_crc32(buf, SLOT_VOLUME_SIZE);
    memcpy(slot_buffer, buf, SLOT_VOLUME_SIZE);
    return SLOT_VOLUME_SIZE;
}

bool storage_codec_decode_volume(const uint8_t *slot_buffer, VolumeStoragePayload *payload)
{
    if (!slot_buffer || !payload) return false;
    memset(payload, 0, sizeof(*payload));
    return true;
}

uint32_t storage_codec_crc32(const uint8_t *data, uint16_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320u : 0);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}
