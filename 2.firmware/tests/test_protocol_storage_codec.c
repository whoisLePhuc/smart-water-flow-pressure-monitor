#include "storage_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_crc_known_answer(void)
{
    static const uint8_t data[] = "123456789";
    assert(storage_codec_crc32(data, 9u) == 0xCBF43926u);
}

static void test_volume_round_trip(void)
{
    uint8_t slot[SLOT_VOLUME_SIZE];
    VolumeStoragePayload input = {
        .sequence = 7u,
        .forward_volume_ul = 1000u,
        .reverse_volume_ul = 500u
    };
    VolumeStoragePayload output;
    memset(&output, 0, sizeof(output));

    assert(storage_codec_encode_volume(slot, &input) == SLOT_VOLUME_SIZE);
    assert(storage_codec_decode_volume(slot, &output));
    assert(output.sequence == input.sequence);
    assert(output.forward_volume_ul == input.forward_volume_ul);
    assert(output.reverse_volume_ul == input.reverse_volume_ul);
}

static void test_decode_rejects_payload_corruption(void)
{
    uint8_t slot[SLOT_VOLUME_SIZE];
    VolumeStoragePayload input = {
        .sequence = 8u,
        .forward_volume_ul = 2000u,
        .reverse_volume_ul = 750u
    };
    VolumeStoragePayload output = {
        .sequence = 99u,
        .forward_volume_ul = 99u,
        .reverse_volume_ul = 99u
    };

    assert(storage_codec_encode_volume(slot, &input) == SLOT_VOLUME_SIZE);
    slot[16] ^= 0xFFu;
    assert(!storage_codec_decode_volume(slot, &output));

    /* A rejected record must not partially overwrite caller-owned output. */
    assert(output.sequence == 99u);
    assert(output.forward_volume_ul == 99u);
    assert(output.reverse_volume_ul == 99u);
}

static void test_decode_rejects_header_corruption(void)
{
    uint8_t slot[SLOT_VOLUME_SIZE];
    VolumeStoragePayload input = {
        .sequence = 9u,
        .forward_volume_ul = 3000u,
        .reverse_volume_ul = 1250u
    };
    VolumeStoragePayload output;

    assert(storage_codec_encode_volume(slot, &input) == SLOT_VOLUME_SIZE);
    slot[0] ^= 0x01u;
    assert(!storage_codec_decode_volume(slot, &output));
}

int main(void)
{
    test_crc_known_answer();
    test_volume_round_trip();
    test_decode_rejects_payload_corruption();
    test_decode_rejects_header_corruption();
    puts("Protocol storage codec: 4 passed, 0 failed");
    return 0;
}
