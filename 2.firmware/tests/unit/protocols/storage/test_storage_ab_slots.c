/**
 * test_storage_ab_slots.c — A/B slot selection tests (STOR-SEL-001, STOR-TRN-001)
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

static void make_valid_slot(uint8_t *buf, uint32_t seq,
                             uint64_t fwd, uint64_t rev)
{
    StorageRecord_EncodeVolume(buf, seq, fwd, rev, 0, 0, 1, 0, 1);
    buf[SLOT_VOLUME_SIZE - 1] = PERSIST_COMMIT_VALID;
}

static void test_both_valid_newer_a(void)
{
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    make_valid_slot(a, 5, 100, 0);
    make_valid_slot(b, 3, 50, 0);

    SlotSelectionResult r = ab_slot_select(a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
                                            PERSIST_RECORD_VOLUME, 1, VOLUME_PAYLOAD_V1_SIZE);
    if (r.status != SLOT_SELECTION_SELECTED || r.selected_slot != 0) {
        FAIL("expected A (newer)"); return;
    }
    PASS();
}

static void test_both_valid_newer_b(void)
{
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    make_valid_slot(a, 2, 100, 0);
    make_valid_slot(b, 7, 50, 0);

    SlotSelectionResult r = ab_slot_select(a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
                                            PERSIST_RECORD_VOLUME, 1, VOLUME_PAYLOAD_V1_SIZE);
    if (r.selected_slot != 1) { FAIL("expected B (newer)"); return; }
    PASS();
}

static void test_only_a_valid(void)
{
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    make_valid_slot(a, 1, 100, 0);
    memset(b, 0, SLOT_VOLUME_SIZE);

    SlotSelectionResult r = ab_slot_select(a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
                                            PERSIST_RECORD_VOLUME, 1, VOLUME_PAYLOAD_V1_SIZE);
    if (r.selected_slot != 0) { FAIL("expected A (only valid)"); return; }
    PASS();
}

static void test_only_b_valid(void)
{
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    memset(a, 0, SLOT_VOLUME_SIZE);
    make_valid_slot(b, 1, 50, 0);

    SlotSelectionResult r = ab_slot_select(a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
                                            PERSIST_RECORD_VOLUME, 1, VOLUME_PAYLOAD_V1_SIZE);
    if (r.selected_slot != 1) { FAIL("expected B (only valid)"); return; }
    PASS();
}

static void test_both_invalid(void)
{
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    memset(a, 0, SLOT_VOLUME_SIZE);
    memset(b, 0, SLOT_VOLUME_SIZE);

    SlotSelectionResult r = ab_slot_select(a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
                                            PERSIST_RECORD_VOLUME, 1, VOLUME_PAYLOAD_V1_SIZE);
    if (r.status != SLOT_SELECTION_NONE ||
        r.selected_slot != SLOT_INDEX_NONE ||
        r.reason_a != SLOT_EMPTY_UNINITIALIZED ||
        r.reason_b != SLOT_EMPTY_UNINITIALIZED) {
        FAIL("expected canonical empty slots"); return;
    }
    PASS();
}

static void test_equal_identical_picks_a(void)
{
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    make_valid_slot(a, 1, 100, 50);
    make_valid_slot(b, 1, 100, 50);  /* same seq, same content */

    SlotSelectionResult r = ab_slot_select(a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
                                            PERSIST_RECORD_VOLUME, 1, VOLUME_PAYLOAD_V1_SIZE);
    if (r.selected_slot != 0) { FAIL("expected A (equal identical)"); return; }
    PASS();
}

static void test_equal_different_is_conflict(void)
{
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    make_valid_slot(a, 10u, 100u, 50u);
    make_valid_slot(b, 10u, 101u, 50u);

    SlotSelectionResult r = ab_slot_select(
        a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
        PERSIST_RECORD_VOLUME, VOLUME_PAYLOAD_V1_SCHEMA,
        VOLUME_PAYLOAD_V1_SIZE);
    if (r.status != SLOT_SELECTION_SEQUENCE_CONFLICT ||
        r.selected_slot != SLOT_INDEX_NONE) {
        FAIL("equal sequence with different content must conflict");
        return;
    }
    PASS();
}

static void test_half_range_is_conflict(void)
{
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    make_valid_slot(a, 0u, 100u, 0u);
    make_valid_slot(b, 0x80000000u, 50u, 0u);

    SlotSelectionResult r = ab_slot_select(
        a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
        PERSIST_RECORD_VOLUME, VOLUME_PAYLOAD_V1_SCHEMA,
        VOLUME_PAYLOAD_V1_SIZE);
    if (r.status != SLOT_SELECTION_SEQUENCE_CONFLICT) {
        FAIL("half-range sequence difference must conflict");
        return;
    }
    PASS();
}

static void test_wraparound_selects_zero_as_newer(void)
{
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    make_valid_slot(a, 0u, 100u, 0u);
    make_valid_slot(b, UINT32_MAX, 50u, 0u);

    SlotSelectionResult r = ab_slot_select(
        a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
        PERSIST_RECORD_VOLUME, VOLUME_PAYLOAD_V1_SCHEMA,
        VOLUME_PAYLOAD_V1_SIZE);
    if (r.status != SLOT_SELECTION_SELECTED || r.selected_slot != 0u) {
        FAIL("sequence zero should be newer after UINT32_MAX");
        return;
    }
    PASS();
}

static void test_future_schema_is_preserved(void)
{
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    make_valid_slot(a, 1u, 100u, 0u);
    make_valid_slot(b, 2u, 200u, 0u);
    b[5] = 2u;
    refresh_crc(b);

    SlotSelectionResult r = ab_slot_select(
        a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
        PERSIST_RECORD_VOLUME, VOLUME_PAYLOAD_V1_SCHEMA,
        VOLUME_PAYLOAD_V1_SIZE);
    if (r.selected_slot != 0u ||
        r.reason_b != SLOT_VALID_UNSUPPORTED_SCHEMA) {
        FAIL("expected compatible A with future-schema evidence");
        return;
    }
    if (ab_slot_choose_target(&r) != SLOT_INDEX_NONE) {
        FAIL("future-schema slot must not be overwritten");
        return;
    }
    PASS();
}

static void test_choose_target_both_valid(void)
{
    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    make_valid_slot(a, 2u, 100u, 0u);
    make_valid_slot(b, 1u, 50u, 0u);
    SlotSelectionResult selection = ab_slot_select(
        a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
        PERSIST_RECORD_VOLUME, VOLUME_PAYLOAD_V1_SCHEMA,
        VOLUME_PAYLOAD_V1_SIZE);
    uint8_t target = ab_slot_choose_target(&selection);
    if (target != 1) { FAIL("expected B when A was boot selected"); return; }

    make_valid_slot(b, 3u, 50u, 0u);
    selection = ab_slot_select(
        a, SLOT_VOLUME_SIZE, b, SLOT_VOLUME_SIZE,
        PERSIST_RECORD_VOLUME, VOLUME_PAYLOAD_V1_SCHEMA,
        VOLUME_PAYLOAD_V1_SIZE);
    target = ab_slot_choose_target(&selection);
    if (target != 0) { FAIL("expected A when B was boot selected"); return; }
    PASS();
}

int main(void)
{
    printf("A/B Slot Selection Tests\n");
    printf("────────────────────────\n");
    test_both_valid_newer_a();
    test_both_valid_newer_b();
    test_only_a_valid();
    test_only_b_valid();
    test_both_invalid();
    test_equal_identical_picks_a();
    test_equal_different_is_conflict();
    test_half_range_is_conflict();
    test_wraparound_selects_zero_as_newer();
    test_future_schema_is_preserved();
    test_choose_target_both_valid();
    printf("────────────────────────\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
