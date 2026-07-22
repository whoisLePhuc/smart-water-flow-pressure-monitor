#include "protocols/storage/storage_record.h"
#include <string.h>


static uint32_t crc_table_init(uint32_t table[256])
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ CRC32_POLY_REFLECTED;
            else
                crc >>= 1;
        }
        table[i] = crc;
    }
    return table[0];  /* return first entry as sentinel */
}

static uint32_t crc32_reflected(const uint8_t *data, uint16_t len)
{
    static uint32_t table[256];
    static int table_ready = 0;
    if (!table_ready) {
        crc_table_init(table);
        table_ready = 1;
    }

    uint32_t crc = CRC32_INIT;
    for (uint16_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ CRC32_XOROUT;
}


static void le_write16(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
}

static void le_write32(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

static void le_write64(uint8_t *buf, uint64_t val)
{
    for (int i = 0; i < 8; i++) {
        buf[i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
}


uint32_t StorageRecord_ComputeCrc(const uint8_t *slot_buffer, uint16_t slot_size)
{
    if (!slot_buffer
        || slot_size < (uint16_t)(PERSIST_COMMON_HEADER_SIZE + 1u)
        || slot_size > 256u)
        return 0u;

    uint16_t payload_length = le_read16(slot_buffer + 6u);
    uint16_t max_payload = (uint16_t)(slot_size -
        PERSIST_COMMON_HEADER_SIZE - 1u);
    if (payload_length > max_payload)
        return 0u;

    /* CRC covers header bytes 0x00..0x0B followed by exactly the declared
     * payload. The CRC field, reserved tail, and commit byte are excluded. */
    const uint16_t crc_header_length = 12u;
    uint8_t buf[256];
    memcpy(buf, slot_buffer, crc_header_length);
    memcpy(buf + crc_header_length,
           slot_buffer + PERSIST_COMMON_HEADER_SIZE,
           payload_length);

    return crc32_reflected(buf,
        (uint16_t)(crc_header_length + payload_length));
}

uint16_t StorageRecord_EncodeVolume(
    uint8_t         *slot_buffer,
    uint32_t        sequence,
    uint64_t        forward_volume_ul,
    uint64_t        reverse_volume_ul,
    uint64_t        forward_remainder,
    uint64_t        reverse_remainder,
    uint64_t        state_version,
    uint64_t        last_flow_sequence,
    uint32_t        last_source_generation)
{
    if (!slot_buffer || forward_remainder >= VOLUME_REMAINDER_LIMIT ||
        reverse_remainder >= VOLUME_REMAINDER_LIMIT)
        return 0;

    memset(slot_buffer, 0, SLOT_VOLUME_SIZE);

    /* Header */
    le_write32(slot_buffer + 0,  PERSIST_MAGIC_U32);
    slot_buffer[4] = PERSIST_RECORD_VOLUME;
    slot_buffer[5] = VOLUME_PAYLOAD_V1_SCHEMA;
    le_write16(slot_buffer + 6,  VOLUME_PAYLOAD_V1_SIZE);
    le_write32(slot_buffer + 8,  sequence);
    /* CRC at offset 0x0C — computed after payload is placed */

    /* Payload (offset 0x10) — volume schema v1 */
    le_write64(slot_buffer + 0x10, forward_volume_ul);
    le_write64(slot_buffer + 0x18, reverse_volume_ul);
    le_write32(slot_buffer + 0x20, (uint32_t)forward_remainder);
    le_write32(slot_buffer + 0x24, (uint32_t)reverse_remainder);
    le_write64(slot_buffer + 0x28, state_version);
    le_write64(slot_buffer + 0x30, last_flow_sequence);
    le_write32(slot_buffer + 0x38, last_source_generation);

    /* Reserved bytes at 0x3C..0x3E are already zero from memset */

    /* CRC over header(0..11) + payload(0x10..0x3B) */
    uint32_t crc = StorageRecord_ComputeCrc(slot_buffer, SLOT_VOLUME_SIZE);
    le_write32(slot_buffer + 0x0C, crc);

    /* Commit byte at 0x3F — PERSIST_COMMIT_INVALID (caller sets VALID after verify) */
    slot_buffer[SLOT_VOLUME_SIZE - 1] = PERSIST_COMMIT_INVALID;

    return SLOT_VOLUME_SIZE;
}

bool StorageRecord_DecodeVolume(
    const uint8_t   *slot_buffer,
    uint64_t        *forward_volume_ul,
    uint64_t        *reverse_volume_ul,
    uint64_t        *forward_remainder,
    uint64_t        *reverse_remainder,
    uint64_t        *state_version,
    uint64_t        *last_flow_sequence,
    uint32_t        *last_source_generation)
{
    if (!slot_buffer)
        return false;

    /* Validate header fields quickly */
    if (le_read32(slot_buffer + 0) != PERSIST_MAGIC_U32)
        return false;
    if (slot_buffer[4] != PERSIST_RECORD_VOLUME)
        return false;
    if (slot_buffer[5] != VOLUME_PAYLOAD_V1_SCHEMA)
        return false;
    if (le_read16(slot_buffer + 6) != VOLUME_PAYLOAD_V1_SIZE)
        return false;

    uint64_t decoded_forward_remainder = le_read32(slot_buffer + 0x20);
    uint64_t decoded_reverse_remainder = le_read32(slot_buffer + 0x24);
    if (decoded_forward_remainder >= VOLUME_REMAINDER_LIMIT ||
        decoded_reverse_remainder >= VOLUME_REMAINDER_LIMIT)
        return false;

    /* Decode payload */
    if (forward_volume_ul)
        *forward_volume_ul     = le_read64(slot_buffer + 0x10);
    if (reverse_volume_ul)
        *reverse_volume_ul     = le_read64(slot_buffer + 0x18);
    if (forward_remainder)
        *forward_remainder     = decoded_forward_remainder;
    if (reverse_remainder)
        *reverse_remainder     = decoded_reverse_remainder;
    if (state_version)
        *state_version         = le_read64(slot_buffer + 0x28);
    if (last_flow_sequence)
        *last_flow_sequence    = le_read64(slot_buffer + 0x30);
    if (last_source_generation)
        *last_source_generation = le_read32(slot_buffer + 0x38);

    return true;
}

SlotClassification StorageRecord_ClassifySlot(
    const uint8_t *slot_buffer,
    uint16_t       slot_size,
    uint8_t        expected_type,
    uint8_t        expected_schema,
    uint16_t       expected_payload_size)
{
    if (!slot_buffer)
        return SLOT_IO_ERROR;
    if (slot_size < (uint16_t)(PERSIST_COMMON_HEADER_SIZE + 1u) ||
        slot_size > 256u)
        return SLOT_BAD_LENGTH;

    bool all_zero = true;
    for (uint16_t i = 0u; i < slot_size; ++i) {
        if (slot_buffer[i] != 0u) {
            all_zero = false;
            break;
        }
    }
    if (all_zero)
        return SLOT_EMPTY_UNINITIALIZED;

    /* 1. Commit byte check */
    uint8_t commit = slot_buffer[slot_size - 1];
    if (commit != PERSIST_COMMIT_VALID)
        return SLOT_NOT_COMMITTED;

    /* 2. Magic */
    if (le_read32(slot_buffer + 0) != PERSIST_MAGIC_U32)
        return SLOT_BAD_MAGIC;

    /* 3. Record type */
    if (slot_buffer[4] != expected_type)
        return SLOT_BAD_TYPE;

    /* 4. Payload length bounds. The exact schema length is checked only
     * after generic integrity validation so a future schema must still be a
     * structurally valid record before it can be preserved as unsupported. */
    uint16_t plen = le_read16(slot_buffer + 6);
    if (plen + PERSIST_COMMON_HEADER_SIZE + 1u > slot_size)
        return SLOT_BAD_LENGTH;

    /* 5. CRC verification */
    uint32_t expected_crc = le_read32(slot_buffer + 0x0C);
    uint32_t actual_crc   = StorageRecord_ComputeCrc(slot_buffer, slot_size);
    if (expected_crc != actual_crc)
        return SLOT_BAD_CRC;

    /* 6. Reserved bytes check (must be zero) */
    uint16_t reserved_start = PERSIST_COMMON_HEADER_SIZE + plen;
    uint16_t reserved_end   = slot_size - 1;  /* before commit byte */
    for (uint16_t i = reserved_start; i < reserved_end; i++) {
        if (slot_buffer[i] != 0)
            return SLOT_BAD_RESERVED;
    }

    /* 7. Schema compatibility and exact known-schema length. */
    if (slot_buffer[5] != expected_schema)
        return SLOT_VALID_UNSUPPORTED_SCHEMA;
    if (plen != expected_payload_size)
        return SLOT_BAD_LENGTH;

    /* 8. Semantic payload validation for known schemas. */
    if (expected_type == PERSIST_RECORD_VOLUME &&
        expected_schema == VOLUME_PAYLOAD_V1_SCHEMA) {
        uint64_t forward_remainder = le_read32(slot_buffer + 0x20);
        uint64_t reverse_remainder = le_read32(slot_buffer + 0x24);
        if (forward_remainder >= VOLUME_REMAINDER_LIMIT ||
            reverse_remainder >= VOLUME_REMAINDER_LIMIT)
            return SLOT_BAD_PAYLOAD;
    }

    return SLOT_VALID_COMPATIBLE;
}
