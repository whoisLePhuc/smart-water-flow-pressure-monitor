#include "services/storage_record.h"
#include <string.h>

/* ── CRC-32/ISO-HDLC table (reflected) ── */

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

/* ── Little-endian helpers ── */

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

/* ── Public API ── */

uint32_t StorageRecord_ComputeCrc(const uint8_t *slot_buffer, uint16_t slot_size)
{
    (void)slot_size;  /* CRC coverage is fixed: header(0..11) + payload(0x10..end-commit) */

    /* CRC covers: bytes 0..0x0B (header without CRC field)
     * then bytes 0x10..(slot_size - 2) (payload + reserved, excluding commit byte).
     * Commit byte is at slot_size - 1. */
    uint16_t payload_start = PERSIST_COMMON_HEADER_SIZE;  /* 0x10 */
    uint16_t crc_len = 12;  /* bytes 0..11 = magic(4) + type(1) + schema(1) + length(2) + sequence(4) */

    uint16_t body_len = slot_size - payload_start - 1;  /* payload + reserved, excluding commit */

    /* Build contiguous CRC input */
    uint8_t buf[256];
    memcpy(buf, slot_buffer, crc_len);
    memcpy(buf + crc_len, slot_buffer + payload_start, body_len);

    return crc32_reflected(buf, crc_len + body_len);
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
    if (!slot_buffer)
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

    /* Decode payload */
    if (forward_volume_ul)
        *forward_volume_ul     = le_read64(slot_buffer + 0x10);
    if (reverse_volume_ul)
        *reverse_volume_ul     = le_read64(slot_buffer + 0x18);
    if (forward_remainder)
        *forward_remainder     = le_read32(slot_buffer + 0x20);
    if (reverse_remainder)
        *reverse_remainder     = le_read32(slot_buffer + 0x24);
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
    if (!slot_buffer || slot_size == 0)
        return SLOT_IO_ERROR;

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

    /* 4. Schema version */
    if (slot_buffer[5] != expected_schema)
        return SLOT_VALID_UNSUPPORTED_SCHEMA;

    /* 5. Payload length */
    uint16_t plen = le_read16(slot_buffer + 6);
    if (plen > expected_payload_size)
        return SLOT_BAD_LENGTH;
    if (plen + PERSIST_COMMON_HEADER_SIZE + 1u > slot_size)
        return SLOT_BAD_LENGTH;

    /* 6. CRC verification */
    uint32_t expected_crc = le_read32(slot_buffer + 0x0C);
    uint32_t actual_crc   = StorageRecord_ComputeCrc(slot_buffer, slot_size);
    if (expected_crc != actual_crc)
        return SLOT_BAD_CRC;

    /* 7. Reserved bytes check (must be zero) */
    uint16_t reserved_start = PERSIST_COMMON_HEADER_SIZE + plen;
    uint16_t reserved_end   = slot_size - 1;  /* before commit byte */
    for (uint16_t i = reserved_start; i < reserved_end; i++) {
        if (slot_buffer[i] != 0)
            return SLOT_BAD_RESERVED;
    }

    return SLOT_VALID_COMPATIBLE;
}
