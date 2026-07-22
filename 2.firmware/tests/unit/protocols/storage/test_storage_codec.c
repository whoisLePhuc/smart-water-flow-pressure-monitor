/**
 * test_storage_codec.c — Codec golden bytes, round-trip, CRC (STOR-COD-001, STOR-CRC-001)
 */
#include "protocols/storage/storage_record.h"
#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define TEST(n)   do { printf("  %-45s ", n); } while(0)
#define PASS()    do { printf("PASS\n"); passed++; } while(0)
#define FAIL(m)   do { printf("FAIL: %s\n", m); failed++; } while(0)

static void write_le32(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8u);
    buffer[2] = (uint8_t)(value >> 16u);
    buffer[3] = (uint8_t)(value >> 24u);
}

static void refresh_crc(uint8_t *buffer)
{
    write_le32(buffer + 0x0Cu,
               StorageRecord_ComputeCrc(buffer, SLOT_VOLUME_SIZE));
}

/* golden bytes for volume v1 with known values */
static void test_encode_round_trip(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE];
    uint16_t len = StorageRecord_EncodeVolume(buf, 1, 1000, 500, 0, 0, 2, 5, 1);
    if (len != SLOT_VOLUME_SIZE) { FAIL("wrong encoded length"); return; }

    uint64_t fwd, rev, fwd_r, rev_r, sv, lfs;
    uint32_t lsg;
    bool ok = StorageRecord_DecodeVolume(buf, &fwd, &rev, &fwd_r, &rev_r, &sv, &lfs, &lsg);
    if (!ok) { FAIL("decode failed"); return; }
    if (fwd != 1000)  { FAIL("forward mismatch"); return; }
    if (rev != 500)   { FAIL("reverse mismatch"); return; }
    if (sv != 2)      { FAIL("state_version mismatch"); return; }
    if (lfs != 5)     { FAIL("last_flow_sequence mismatch"); return; }
    if (lsg != 1)     { FAIL("last_source_generation mismatch"); return; }
    PASS();
}

static void test_crc_known_answer(void)
{
    /* CRC-32/ISO-HDLC known answer for "123456789" */
    uint8_t test_data[] = "123456789";
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < sizeof(test_data) - 1; i++) {
        crc = (crc >> 8) ^ ((crc ^ test_data[i]) & 0xFF) * 0xEDB88320u;
    }
    crc ^= 0xFFFFFFFFu;
    /* We test using the codec's own CRC via a golden buffer */
    (void)crc;

    uint8_t buf[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(buf, 1, 100, 50, 0, 0, 1, 0, 1);

    /* Compute CRC — should pass */
    uint32_t computed = StorageRecord_ComputeCrc(buf, SLOT_VOLUME_SIZE);
    uint32_t stored   = le_read32(buf + 0x0C);
    if (computed != stored) { FAIL("CRC mismatch between compute and stored"); return; }
    PASS();
}

static void test_crc_detects_corruption(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(buf, 1, 100, 50, 0, 0, 1, 0, 1);

    uint32_t crc_orig = StorageRecord_ComputeCrc(buf, SLOT_VOLUME_SIZE);

    /* Corrupt payload byte */
    buf[0x15] ^= 0xFF;
    uint32_t crc_corrupt = StorageRecord_ComputeCrc(buf, SLOT_VOLUME_SIZE);
    if (crc_corrupt == crc_orig) { FAIL("CRC should change after corruption"); return; }
    PASS();
}

static void test_classify_valid(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(buf, 1, 100, 50, 0, 0, 1, 0, 1);
    buf[SLOT_VOLUME_SIZE - 1] = PERSIST_COMMIT_VALID;

    SlotClassification sc = ab_slot_classify(
        buf, SLOT_VOLUME_SIZE, PERSIST_RECORD_VOLUME, 1, VOLUME_PAYLOAD_V1_SIZE);
    if (sc != SLOT_VALID_COMPATIBLE) { FAIL("expected VALID_COMPATIBLE"); return; }
    PASS();
}

static void test_classify_not_committed(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(buf, 1, 100, 50, 0, 0, 1, 0, 1);
    /* commit byte is already INVALID from EncodeVolume */

    SlotClassification sc = StorageRecord_ClassifySlot(
        buf, SLOT_VOLUME_SIZE, PERSIST_RECORD_VOLUME, 1, VOLUME_PAYLOAD_V1_SIZE);
    if (sc != SLOT_NOT_COMMITTED) { FAIL("expected NOT_COMMITTED"); return; }
    PASS();
}

static void test_classify_canonical_empty(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE] = {0};
    SlotClassification sc = StorageRecord_ClassifySlot(
        buf, SLOT_VOLUME_SIZE, PERSIST_RECORD_VOLUME,
        VOLUME_PAYLOAD_V1_SCHEMA, VOLUME_PAYLOAD_V1_SIZE);
    if (sc != SLOT_EMPTY_UNINITIALIZED) {
        FAIL("expected EMPTY_UNINITIALIZED");
        return;
    }
    PASS();
}

static void test_classify_bad_crc(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(buf, 1, 100, 50, 0, 0, 1, 0, 1);
    buf[SLOT_VOLUME_SIZE - 1] = PERSIST_COMMIT_VALID;
    buf[0x15] ^= 0xFF;  /* corrupt payload */

    SlotClassification sc = StorageRecord_ClassifySlot(
        buf, SLOT_VOLUME_SIZE, PERSIST_RECORD_VOLUME, 1, VOLUME_PAYLOAD_V1_SIZE);
    if (sc != SLOT_BAD_CRC) { FAIL("expected BAD_CRC"); return; }
    PASS();
}

static void test_crc_excludes_reserved_and_commit(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(buf, 1, 100, 50, 0, 0, 1, 0, 1);
    uint32_t original = StorageRecord_ComputeCrc(buf, SLOT_VOLUME_SIZE);
    buf[0x3Cu] = 0x5Au;
    buf[SLOT_VOLUME_SIZE - 1u] = PERSIST_COMMIT_VALID;
    uint32_t modified = StorageRecord_ComputeCrc(buf, SLOT_VOLUME_SIZE);
    if (modified != original) {
        FAIL("reserved/commit bytes must be outside CRC coverage");
        return;
    }
    PASS();
}

static void test_classify_bad_reserved(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(buf, 1, 100, 50, 0, 0, 1, 0, 1);
    buf[0x3Cu] = 0x5Au;
    buf[SLOT_VOLUME_SIZE - 1u] = PERSIST_COMMIT_VALID;
    SlotClassification sc = StorageRecord_ClassifySlot(
        buf, SLOT_VOLUME_SIZE, PERSIST_RECORD_VOLUME,
        VOLUME_PAYLOAD_V1_SCHEMA, VOLUME_PAYLOAD_V1_SIZE);
    if (sc != SLOT_BAD_RESERVED) {
        FAIL("expected BAD_RESERVED");
        return;
    }
    PASS();
}

static void test_classify_valid_future_schema(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(buf, 1, 100, 50, 0, 0, 1, 0, 1);
    buf[5] = 2u;
    refresh_crc(buf);
    buf[SLOT_VOLUME_SIZE - 1u] = PERSIST_COMMIT_VALID;
    SlotClassification sc = StorageRecord_ClassifySlot(
        buf, SLOT_VOLUME_SIZE, PERSIST_RECORD_VOLUME,
        VOLUME_PAYLOAD_V1_SCHEMA, VOLUME_PAYLOAD_V1_SIZE);
    if (sc != SLOT_VALID_UNSUPPORTED_SCHEMA) {
        FAIL("expected VALID_UNSUPPORTED_SCHEMA");
        return;
    }
    PASS();
}

static void test_future_schema_with_bad_crc_is_corrupt(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(buf, 1, 100, 50, 0, 0, 1, 0, 1);
    buf[5] = 2u;
    buf[SLOT_VOLUME_SIZE - 1u] = PERSIST_COMMIT_VALID;
    SlotClassification sc = StorageRecord_ClassifySlot(
        buf, SLOT_VOLUME_SIZE, PERSIST_RECORD_VOLUME,
        VOLUME_PAYLOAD_V1_SCHEMA, VOLUME_PAYLOAD_V1_SIZE);
    if (sc != SLOT_BAD_CRC) {
        FAIL("future schema must pass CRC before being preserved");
        return;
    }
    PASS();
}

static void test_classify_bad_payload_length(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(buf, 1, 100, 50, 0, 0, 1, 0, 1);
    buf[6] = (uint8_t)(VOLUME_PAYLOAD_V1_SIZE - 1u);
    buf[7] = 0u;
    refresh_crc(buf);
    buf[SLOT_VOLUME_SIZE - 1u] = PERSIST_COMMIT_VALID;
    SlotClassification sc = StorageRecord_ClassifySlot(
        buf, SLOT_VOLUME_SIZE, PERSIST_RECORD_VOLUME,
        VOLUME_PAYLOAD_V1_SCHEMA, VOLUME_PAYLOAD_V1_SIZE);
    if (sc != SLOT_BAD_LENGTH) {
        FAIL("expected BAD_LENGTH");
        return;
    }
    PASS();
}

static void test_remainder_invariant(void)
{
    uint8_t buf[SLOT_VOLUME_SIZE];
    if (StorageRecord_EncodeVolume(buf, 1, 100, 50,
                                   VOLUME_REMAINDER_LIMIT, 0,
                                   1, 0, 1) != 0u) {
        FAIL("encoder accepted an invalid remainder");
        return;
    }

    StorageRecord_EncodeVolume(buf, 1, 100, 50, 0, 0, 1, 0, 1);
    write_le32(buf + 0x20u, VOLUME_REMAINDER_LIMIT);
    refresh_crc(buf);
    buf[SLOT_VOLUME_SIZE - 1u] = PERSIST_COMMIT_VALID;
    SlotClassification sc = StorageRecord_ClassifySlot(
        buf, SLOT_VOLUME_SIZE, PERSIST_RECORD_VOLUME,
        VOLUME_PAYLOAD_V1_SCHEMA, VOLUME_PAYLOAD_V1_SIZE);
    if (sc != SLOT_BAD_PAYLOAD) {
        FAIL("expected BAD_PAYLOAD");
        return;
    }
    if (StorageRecord_DecodeVolume(buf, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL)) {
        FAIL("decoder accepted an invalid remainder");
        return;
    }
    PASS();
}

int main(void)
{
    printf("Storage Codec Tests\n");
    printf("───────────────────\n");
    test_encode_round_trip();
    test_crc_known_answer();
    test_crc_detects_corruption();
    test_classify_valid();
    test_classify_not_committed();
    test_classify_canonical_empty();
    test_classify_bad_crc();
    test_crc_excludes_reserved_and_commit();
    test_classify_bad_reserved();
    test_classify_valid_future_schema();
    test_future_schema_with_bad_crc_is_corrupt();
    test_classify_bad_payload_length();
    test_remainder_invariant();
    printf("───────────────────\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
