#ifndef SWFPM_STORAGE_CODEC_H
#define SWFPM_STORAGE_CODEC_H

#include <stdint.h>
#include <stdbool.h>

#define SLOT_VOLUME_SIZE 64u

typedef struct {
    uint32_t sequence;
    uint64_t forward_volume_ul;
    uint64_t reverse_volume_ul;
} VolumeStoragePayload;

uint16_t storage_codec_encode_volume(uint8_t *slot_buffer, const VolumeStoragePayload *payload);
bool storage_codec_decode_volume(const uint8_t *slot_buffer, VolumeStoragePayload *payload);
uint32_t storage_codec_crc32(const uint8_t *data, uint16_t len);

#endif
