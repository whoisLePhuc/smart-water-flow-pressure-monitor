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
    if (r.selected_slot != 0) { FAIL("expected A (newer)"); return; }
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
    if (r.selected_slot != 0xFF) { FAIL("expected none valid"); return; }
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

static void test_choose_target_both_valid(void)
{
    uint8_t target = ab_slot_choose_target(true, true, 0);
    if (target != 1) { FAIL("expected B when A was boot selected"); return; }
    target = ab_slot_choose_target(true, true, 1);
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
    test_choose_target_both_valid();
    printf("────────────────────────\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
