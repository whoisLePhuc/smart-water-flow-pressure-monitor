#ifndef SWFPM_STORAGE_RECORD_H
#define SWFPM_STORAGE_RECORD_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* =================================================================
 * Persistent storage constants (FW-DATA-022 v0.1)
 * ================================================================= */

/* F-RAM geometry */
#define FM24CL04B_SIZE_BYTES        512u
#define PERSIST_COMMON_HEADER_SIZE  16u

/* Commit byte values */
#define PERSIST_COMMIT_VALID        0xA5u
#define PERSIST_COMMIT_INVALID      0x00u

/* Magic: "SWFP" = 0x53574650, little-endian on media => 50 46 57 53 */
#define PERSIST_MAGIC_U32           0x53574650u
#define PERSIST_MAGIC_SIZE          4u

/* Record type identifiers */
typedef enum {
    PERSIST_RECORD_CONFIG       = 1,
    PERSIST_RECORD_CALIBRATION  = 2,
    PERSIST_RECORD_VOLUME       = 3
} PersistentRecordType;

/* =================================================================
 * Memory map — 512-byte FM24CL04B (FW-DATA-022 §8.1)
 * ================================================================= */

/* | Start | End   | Size | Region     | Slot |
 * | 0x000 | 0x03F | 64 B | CONFIG     | A    |
 * | 0x040 | 0x07F | 64 B | CONFIG     | B    |
 * | 0x080 | 0x0DF | 96 B | CALIBRATION | A   |
 * | 0x0E0 | 0x13F | 96 B | CALIBRATION | B   |
 * | 0x140 | 0x17F | 64 B | VOLUME      | A   |
 * | 0x180 | 0x1BF | 64 B | VOLUME      | B   |
 * | 0x1C0 | 0x1FF | 64 B | RESERVED    | —   |
 */

#define SLOT_VOLUME_A_ADDR          0x140u
#define SLOT_VOLUME_B_ADDR          0x180u
#define SLOT_VOLUME_SIZE            64u
#define SLOT_CALIBRATION_A_ADDR     0x080u
#define SLOT_CALIBRATION_B_ADDR     0x0E0u
#define SLOT_CALIBRATION_SIZE       96u
#define SLOT_CONFIG_A_ADDR          0x000u
#define SLOT_CONFIG_B_ADDR          0x040u
#define SLOT_CONFIG_SIZE            64u
#define SLOT_RESERVED_ADDR          0x1C0u
#define SLOT_RESERVED_SIZE          64u

/* Maximum payload per slot type */
#define MAX_PAYLOAD_VOLUME          (SLOT_VOLUME_SIZE - PERSIST_COMMON_HEADER_SIZE - 1u)   /* 47 B */
#define MAX_PAYLOAD_CALIBRATION     (SLOT_CALIBRATION_SIZE - PERSIST_COMMON_HEADER_SIZE - 1u) /* 79 B */
#define MAX_PAYLOAD_CONFIG          (SLOT_CONFIG_SIZE - PERSIST_COMMON_HEADER_SIZE - 1u)    /* 47 B */

/* =================================================================
 * Common slot layout
 * =================================================================
 * offset  size  field
 * 0x00    4     magic_u32
 * 0x04    1     record_type_u8
 * 0x05    1     schema_version_u8
 * 0x06    2     payload_length_u16 (little-endian)
 * 0x08    4     sequence_u32 (little-endian)
 * 0x0C    4     crc32_u32 (little-endian)
 * 0x10    N     payload
 * ...           reserved bytes = 0x00
 * last    1     commit_u8
 */

typedef struct {
    uint32_t    magic;
    uint8_t     record_type;
    uint8_t     schema_version;
    uint16_t    payload_length;     /* little-endian on media */
    uint32_t    sequence;           /* little-endian on media */
    uint32_t    crc32;              /* little-endian on media, CRC-32/ISO-HDLC */
    uint8_t     payload[64];        /* max slot payload area (safe for all types) */
    uint8_t     commit;
} PersistentRecordHeader;

_Static_assert(sizeof(PersistentRecordHeader) >= 16, "PersistentRecordHeader too small");

/* =================================================================
 * Volume payload schema v1 (44 bytes payload, fits 64B slot)
 * =================================================================
 * Payload offset  Size  Field
 * 0x00            8     forward_volume_ul (le)
 * 0x08            8     reverse_volume_ul (le)
 * 0x10            4     forward_remainder (le)
 * 0x14            4     reverse_remainder (le)
 * 0x18            8     volume_state_version (le)
 * 0x20            8     last_consumed_flow_sequence (le)
 * 0x28            4     last_consumed_source_generation (le)
 *
 * reserved bytes 0x2C..0x3E = 0x00
 * commit byte 0x3F
 */

#define VOLUME_PAYLOAD_V1_SIZE      44u
#define VOLUME_PAYLOAD_V1_SCHEMA    1u

/* CRC-32/ISO-HDLC parameters */
#define CRC32_POLY_REFLECTED        0xEDB88320u
#define CRC32_INIT                  0xFFFFFFFFu
#define CRC32_XOROUT                0xFFFFFFFFu

/* =================================================================
 * Slot classification outcomes (FW-DATA-022 §9.1)
 * ================================================================= */

typedef enum {
    SLOT_VALID_COMPATIBLE,
    SLOT_VALID_UNSUPPORTED_SCHEMA,
    SLOT_VALID_INCOMPATIBLE,
    SLOT_EMPTY_UNINITIALIZED,
    SLOT_NOT_COMMITTED,
    SLOT_BAD_MAGIC,
    SLOT_BAD_TYPE,
    SLOT_BAD_LENGTH,
    SLOT_BAD_RESERVED,
    SLOT_BAD_CRC,
    SLOT_BAD_PAYLOAD,
    SLOT_IO_ERROR
} SlotClassification;

/* ── Little-endian read helpers (shared with ab_slot module) ── */

static inline uint16_t le_read16(const uint8_t *buf) {
    return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}
static inline uint32_t le_read32(const uint8_t *buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8)
         | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}
static inline uint64_t le_read64(const uint8_t *buf) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | buf[i];
    return v;
}

/* =================================================================
 * Codec API (pure, no I2C/clock/event)
 * ================================================================= */

/* ── A/B slot selection result ── */

typedef struct {
    bool     slot_a_valid;
    bool     slot_b_valid;
    uint32_t sequence_a;
    uint32_t sequence_b;
    uint8_t  selected_slot;   /* 0 = A, 1 = B, 0xFF = none/conflict */
    SlotClassification reason_a;
    SlotClassification reason_b;
} SlotSelectionResult;

/* Encode volume v1 payload into record buffer (slot-sized).
 * Returns encoded length, or 0 on error. */
uint16_t StorageRecord_EncodeVolume(
    uint8_t         *slot_buffer,       /* [out] SLOT_VOLUME_SIZE bytes */
    uint32_t        sequence,
    uint64_t        forward_volume_ul,
    uint64_t        reverse_volume_ul,
    uint64_t        forward_remainder,
    uint64_t        reverse_remainder,
    uint64_t        state_version,
    uint64_t        last_flow_sequence,
    uint32_t        last_source_generation);

/* Decode volume v1 payload from a validated slot buffer.
 * Returns true on success. */
bool StorageRecord_DecodeVolume(
    const uint8_t   *slot_buffer,       /* [in] SLOT_VOLUME_SIZE bytes */
    uint64_t        *forward_volume_ul,
    uint64_t        *reverse_volume_ul,
    uint64_t        *forward_remainder,
    uint64_t        *reverse_remainder,
    uint64_t        *state_version,
    uint64_t        *last_flow_sequence,
    uint32_t        *last_source_generation);

/* CRC-32/ISO-HDLC over header (bytes 0..0x0B) + payload (bytes 0x10..0x10+len-1).
 * Does NOT cover CRC field, reserved bytes, or commit byte. */
uint32_t StorageRecord_ComputeCrc(const uint8_t *slot_buffer, uint16_t slot_size);

/* ── A/B slot selection and commit target ── */

/* Classify a single slot. */
SlotClassification ab_slot_classify(
    const uint8_t *slot_buffer,
    uint16_t       slot_size,
    uint8_t        expected_type,
    uint8_t        expected_schema,
    uint16_t       expected_payload_size);

/* Select best valid slot at boot. */
SlotSelectionResult ab_slot_select(
    const uint8_t *buf_a, uint16_t size_a,
    const uint8_t *buf_b, uint16_t size_b,
    uint8_t        expected_type,
    uint8_t        expected_schema,
    uint16_t       expected_payload_size);

/* Choose target slot for next commit. Returns 0=A, 1=B. */
uint8_t ab_slot_choose_target(bool slot_a_valid, bool slot_b_valid,
                               uint8_t boot_selected);

/* Classify a slot buffer by structural validation (no I2C). */
SlotClassification StorageRecord_ClassifySlot(
    const uint8_t *slot_buffer,
    uint16_t       slot_size,
    uint8_t        expected_type,
    uint8_t        expected_schema,
    uint16_t       expected_payload_size);

#endif /* SWFPM_STORAGE_RECORD_H */
