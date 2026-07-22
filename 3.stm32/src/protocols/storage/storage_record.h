#ifndef SWFPM_STORAGE_RECORD_H
#define SWFPM_STORAGE_RECORD_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>


/* F-RAM geometry */
#define FM24CL04B_SIZE_BYTES 512u
#define PERSIST_COMMON_HEADER_SIZE 16u

/* Commit byte values */
#define PERSIST_COMMIT_VALID 0xA5u
#define PERSIST_COMMIT_INVALID 0x00u

/* Magic: "SWFP" = 0x53574650, little-endian on media => 50 46 57 53 */
#define PERSIST_MAGIC_U32 0x53574650u
#define PERSIST_MAGIC_SIZE 4u

/* Record type identifiers */
/** @brief Identifiers for persistent record types stored in F-RAM A/B slots. */
typedef enum {
    PERSIST_RECORD_CONFIG = 1,        /**< System configuration parameters. */
    PERSIST_RECORD_CALIBRATION = 2,   /**< Sensor calibration data. */
    PERSIST_RECORD_VOLUME = 3         /**< Cumulative water volume snapshot. */
} PersistentRecordType;


/* | Start | End   | Size | Region     | Slot |
 * | 0x000 | 0x03F | 64 B | CONFIG     | A    |
 * | 0x040 | 0x07F | 64 B | CONFIG     | B    |
 * | 0x080 | 0x0DF | 96 B | CALIBRATION | A   |
 * | 0x0E0 | 0x13F | 96 B | CALIBRATION | B   |
 * | 0x140 | 0x17F | 64 B | VOLUME      | A   |
 * | 0x180 | 0x1BF | 64 B | VOLUME      | B   |
 * | 0x1C0 | 0x1FF | 64 B | RESERVED    | —   |
 */

#define SLOT_VOLUME_A_ADDR 0x140u
#define SLOT_VOLUME_B_ADDR 0x180u
#define SLOT_VOLUME_SIZE 64u
#define SLOT_CALIBRATION_A_ADDR 0x080u
#define SLOT_CALIBRATION_B_ADDR 0x0E0u
#define SLOT_CALIBRATION_SIZE 96u
#define SLOT_CONFIG_A_ADDR 0x000u
#define SLOT_CONFIG_B_ADDR 0x040u
#define SLOT_CONFIG_SIZE 64u
#define SLOT_RESERVED_ADDR 0x1C0u
#define SLOT_RESERVED_SIZE 64u

/* Maximum payload per slot type */
#define MAX_PAYLOAD_VOLUME (SLOT_VOLUME_SIZE - PERSIST_COMMON_HEADER_SIZE - 1u) /* 47 B */
#define MAX_PAYLOAD_CALIBRATION                                                          \
    (SLOT_CALIBRATION_SIZE - PERSIST_COMMON_HEADER_SIZE - 1u)                   /* 79 B */
#define MAX_PAYLOAD_CONFIG (SLOT_CONFIG_SIZE - PERSIST_COMMON_HEADER_SIZE - 1u) /* 47 B */

/* On-media common slot layout. Fields are encoded explicitly; this is not the
 * in-memory layout of PersistentRecordHeader.
 *
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

/** @brief On-media slot header with explicit little-endian encoding. */
typedef struct {
    uint32_t magic;              /**< Magic value PERSIST_MAGIC_U32 for slot identification. */
    uint8_t record_type;         /**< Record type identifier (PersistentRecordType). */
    uint8_t schema_version;      /**< Payload schema version for forward compatibility. */
    uint16_t payload_length;     /**< Payload byte count (little-endian on media). */
    uint32_t sequence;           /**< Monotonic sequence number (little-endian on media). */
    uint32_t crc32;              /**< CRC-32/ISO-HDLC over header + payload (little-endian). */
    uint8_t payload[64];         /**< Slot payload area, sized for the largest slot type. */
    uint8_t commit;              /**< Commit marker: PERSIST_COMMIT_VALID or _INVALID. */
} PersistentRecordHeader;

_Static_assert(sizeof(PersistentRecordHeader) >= 16, "PersistentRecordHeader too small");

/* Volume payload schema v1 (44 bytes payload, fits a 64-byte slot).
 *
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

#define VOLUME_PAYLOAD_V1_SIZE 44u
#define VOLUME_PAYLOAD_V1_SCHEMA 1u
#define VOLUME_REMAINDER_LIMIT 1000000u

/* CRC-32/ISO-HDLC parameters */
#define CRC32_POLY_REFLECTED 0xEDB88320u
#define CRC32_INIT 0xFFFFFFFFu
#define CRC32_XOROUT 0xFFFFFFFFu


/** @brief Classification result from slot validation. */
typedef enum {
    SLOT_VALID_COMPATIBLE,          /**< Slot is valid and schema is supported. */
    SLOT_VALID_UNSUPPORTED_SCHEMA,  /**< Slot is valid but schema version is unknown. */
    SLOT_VALID_INCOMPATIBLE,        /**< Slot is valid but payload is incompatible. */
    SLOT_EMPTY_UNINITIALIZED,       /**< Slot appears uninitialized (all zeros). */
    SLOT_NOT_COMMITTED,             /**< Commit byte not set to VALID. */
    SLOT_BAD_MAGIC,                 /**< Magic value mismatch. */
    SLOT_BAD_TYPE,                  /**< Record type does not match expected. */
    SLOT_BAD_LENGTH,                /**< Payload length exceeds slot capacity. */
    SLOT_BAD_RESERVED,              /**< Reserved bytes are non-zero. */
    SLOT_BAD_CRC,                   /**< CRC-32 checksum mismatch. */
    SLOT_BAD_PAYLOAD,               /**< Payload content is semantically invalid. */
    SLOT_IO_ERROR                   /**< I/O error reading the slot. */
} SlotClassification;

/** @brief Terminal outcome of selecting between A/B slots. */
typedef enum {
    SLOT_SELECTION_NONE,               /**< Neither slot is valid and compatible. */
    SLOT_SELECTION_SELECTED,           /**< selected_slot identifies the active slot. */
    SLOT_SELECTION_SEQUENCE_CONFLICT   /**< Sequence ordering is ambiguous or conflicting. */
} SlotSelectionStatus;

#define SLOT_INDEX_NONE 0xFFu


/** @brief Read a 16-bit unsigned integer from a little-endian buffer. */
static inline uint16_t le_read16(const uint8_t* buf) {
    return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}
/** @brief Read a 32-bit unsigned integer from a little-endian buffer. */
static inline uint32_t le_read32(const uint8_t* buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16)
           | ((uint32_t)buf[3] << 24);
}
/** @brief Read a 64-bit unsigned integer from a little-endian buffer. */
static inline uint64_t le_read64(const uint8_t* buf) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | buf[i];
    return v;
}


/** @brief Result of A/B slot selection at boot. */
typedef struct {
    bool slot_a_valid;                  /**< Slot A is valid and compatible. */
    bool slot_b_valid;                  /**< Slot B is valid and compatible. */
    uint32_t sequence_a;                /**< Sequence number of slot A (if valid). */
    uint32_t sequence_b;                /**< Sequence number of slot B (if valid). */
    uint8_t selected_slot;              /**< Selected slot: 0 = A, 1 = B, 0xFF = none/conflict. */
    SlotSelectionStatus status;         /**< Terminal selection outcome. */
    SlotClassification reason_a;        /**< Classification reason for slot A. */
    SlotClassification reason_b;        /**< Classification reason for slot B. */
} SlotSelectionResult;

/** @brief Encode a volume v1 payload into a slot-sized buffer.
 *  @param[out] slot_buffer            Target buffer (SLOT_VOLUME_SIZE bytes).
 *  @param[in]  sequence               Monotonic sequence number.
 *  @param[in]  forward_volume_ul      Cumulative forward volume in microlitres.
 *  @param[in]  reverse_volume_ul      Cumulative reverse volume in microlitres.
 *  @param[in]  forward_remainder      Forward remainder carry (sub-ul).
 *  @param[in]  reverse_remainder      Reverse remainder carry (sub-ul).
 *  @param[in]  state_version          Volume state version for change tracking.
 *  @param[in]  last_flow_sequence     Sequence of the last consumed flow sample.
 *  @param[in]  last_source_generation Generation counter of the last flow source.
 *  @return Encoded length in bytes, or 0 on error. */
uint16_t
StorageRecord_EncodeVolume(uint8_t* slot_buffer, /* [out] SLOT_VOLUME_SIZE bytes */
                           uint32_t sequence,
                           uint64_t forward_volume_ul,
                           uint64_t reverse_volume_ul,
                           uint64_t forward_remainder,
                           uint64_t reverse_remainder,
                           uint64_t state_version,
                           uint64_t last_flow_sequence,
                           uint32_t last_source_generation);

/** @brief Decode a volume v1 payload from a validated slot buffer.
 *  @param[in]  slot_buffer              Source buffer (SLOT_VOLUME_SIZE bytes).
 *  @param[out] forward_volume_ul        Decoded forward volume, or NULL to skip.
 *  @param[out] reverse_volume_ul        Decoded reverse volume, or NULL to skip.
 *  @param[out] forward_remainder        Decoded forward remainder, or NULL to skip.
 *  @param[out] reverse_remainder        Decoded reverse remainder, or NULL to skip.
 *  @param[out] state_version            Decoded volume state version, or NULL to skip.
 *  @param[out] last_flow_sequence       Decoded last flow sequence, or NULL to skip.
 *  @param[out] last_source_generation   Decoded last source generation, or NULL to skip.
 *  @return true on successful decode, false on validation failure. */
bool StorageRecord_DecodeVolume(
    const uint8_t* slot_buffer, /* [in] SLOT_VOLUME_SIZE bytes */
    uint64_t* forward_volume_ul,
    uint64_t* reverse_volume_ul,
    uint64_t* forward_remainder,
    uint64_t* reverse_remainder,
    uint64_t* state_version,
    uint64_t* last_flow_sequence,
    uint32_t* last_source_generation);

/** @brief Compute CRC-32/ISO-HDLC over header and payload of a slot buffer.
 *  @param[in] slot_buffer  Slot buffer to checksum.
 *  @param[in] slot_size    Total slot size in bytes.
 *  @return CRC-32/ISO-HDLC hash, or 0 if parameters are invalid.
 *
 *  Coverage: header bytes 0..0x0B and exactly payload_length bytes from 0x10.
 *  Does NOT cover the CRC field, reserved bytes, or commit byte. */
uint32_t StorageRecord_ComputeCrc(const uint8_t* slot_buffer, uint16_t slot_size);


/** @brief Classify a single A/B slot by validating its content.
 *  @param[in] slot_buffer          Slot buffer to validate.
 *  @param[in] slot_size            Total slot size in bytes.
 *  @param[in] expected_type        Expected record type identifier.
 *  @param[in] expected_schema      Expected payload schema version.
 *  @param[in] expected_payload_size Expected payload byte count.
 *  @return SlotClassification result. */
SlotClassification ab_slot_classify(const uint8_t* slot_buffer,
                                    uint16_t slot_size,
                                    uint8_t expected_type,
                                    uint8_t expected_schema,
                                    uint16_t expected_payload_size);

/** @brief Select the best valid slot between A and B at boot.
 *  @param[in] buf_a               Buffer for slot A.
 *  @param[in] size_a              Size of slot A buffer.
 *  @param[in] buf_b               Buffer for slot B.
 *  @param[in] size_b              Size of slot B buffer.
 *  @param[in] expected_type       Expected record type identifier.
 *  @param[in] expected_schema     Expected payload schema version.
 *  @param[in] expected_payload_size Expected payload byte count.
 *  @return SlotSelectionResult with the chosen slot and per-slot reasons. */
SlotSelectionResult ab_slot_select(const uint8_t* buf_a,
                                   uint16_t size_a,
                                   const uint8_t* buf_b,
                                   uint16_t size_b,
                                   uint8_t expected_type,
                                   uint8_t expected_schema,
                                   uint16_t expected_payload_size);

/** @brief Choose a safe target slot for the next commit.
 *  @param[in] selection Result of scanning and selecting the current A/B slots.
 *  @return Target slot: 0 = A, 1 = B, or SLOT_INDEX_NONE when neither slot
 *          can be overwritten without losing conflict/future-schema evidence. */
uint8_t ab_slot_choose_target(const SlotSelectionResult* selection);

/** @brief Validate a slot buffer by checking magic, type, CRC, and reserved bytes.
 *  @param[in] slot_buffer          Slot buffer to classify.
 *  @param[in] slot_size            Total slot size in bytes.
 *  @param[in] expected_type        Expected record type identifier.
 *  @param[in] expected_schema      Expected payload schema version.
 *  @param[in] expected_payload_size Expected payload byte count.
 *  @return SlotClassification result. */
SlotClassification StorageRecord_ClassifySlot(const uint8_t* slot_buffer,
                                              uint16_t slot_size,
                                              uint8_t expected_type,
                                              uint8_t expected_schema,
                                              uint16_t expected_payload_size);

#endif /* SWFPM_STORAGE_RECORD_H */
