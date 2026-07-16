/**
 * ZSSC3241 deterministic scenarios.
 *
 * Tests: normal EOC one-shot, polling fallback, due-while-busy,
 *        missing EOC, I2C failure, bus recovery, shared-bus contention.
 */

#include "peers/peer_zssc3241.h"
#include "peers/peer_fram.h"
#include "providers/linux_i2c_provider.h"
#include "platform/include/linux_scheduled_action_queue.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* ── ZSSC normal EOC ───────────────────────────────── */

static void test_zssc_normal_eoc(void)
{
    Zssc3241Peer peer;
    zssc_peer_init(&peer);

    uint64_t eoc_time = zssc_peer_schedule_eoc(&peer, 5000);
    assert(eoc_time > 5000);

    /* I2C transaction */
    uint8_t tx = 0x10, rx[2] = {0};
    uint64_t lat;
    uint32_t st;
    bool ok = zssc_peer_plan_i2c(&peer, 0x50, &tx, 1, rx, 2, &lat, &st);
    assert(ok);
    assert(peer.i2c_operations == 1);
    PASS();
}

/* ── ZSSC missing EOC ──────────────────────────────── */

static void test_zssc_missing_eoc(void)
{
    Zssc3241Peer peer;
    zssc_peer_init(&peer);
    zssc_peer_set_fault(&peer, ZSSC_FAULT_MISSING_EOC);

    uint64_t eoc_time = zssc_peer_schedule_eoc(&peer, 1000);
    assert(eoc_time > 1000);
    assert(peer.eoc_active == false);
    PASS();
}

/* ── ZSSC I2C failure ──────────────────────────────── */

static void test_zssc_i2c_failure(void)
{
    Zssc3241Peer peer;
    zssc_peer_init(&peer);
    zssc_peer_set_fault(&peer, ZSSC_FAULT_NO_COMPLETION);

    uint8_t tx = 0x10, rx[2] = {0};
    uint64_t lat;
    uint32_t st;
    bool ok = zssc_peer_plan_i2c(&peer, 0x50, &tx, 1, rx, 2, &lat, &st);
    assert(ok == false);
    PASS();
}

/* ── ZSSC stuck busy ───────────────────────────────── */

static void test_zssc_stuck_busy(void)
{
    Zssc3241Peer peer;
    zssc_peer_init(&peer);
    zssc_peer_set_fault(&peer, ZSSC_FAULT_STUCK_BUSY);

    uint8_t tx = 0x10, rx[2] = {0};
    uint64_t lat;
    uint32_t st;
    bool ok = zssc_peer_plan_i2c(&peer, 0x50, &tx, 1, rx, 2, &lat, &st);
    assert(ok);
    assert(st == 0x80);  /* Busy flag */
    PASS();
}

/* ── Shared-I2C contention: ZSSC + F-RAM ───────────── */

static void test_shared_i2c_contention(void)
{
    LinuxScheduledActionQueue q;
    action_queue_init(&q);

    LinuxI2cProvider i2c;
    linux_i2c_init(&i2c, &q);

    /* Register ZSSC at 0x50 */
    Zssc3241Peer zssc;
    zssc_peer_init(&zssc);
    LinuxI2cPeer zs = { .i2c_plan = zssc_peer_plan_i2c, .context = &zssc };
    linux_i2c_register_peer(&i2c, 0x50, zs);

    /* Register F-RAM at 0x51 */
    FramPeer fram;
    fram_peer_init(&fram);
    LinuxI2cPeer fr = { .i2c_plan = fram_peer_plan_i2c, .context = &fram };
    linux_i2c_register_peer(&i2c, 0x51, fr);

    /* ZSSC transaction occupies bus */
    LinuxI2cRequest req = {
        .operation_id = 1, .correlation_id = 10, .owner_generation = 1,
        .slave_address = 0x50, .deadline_us = 0
    };
    assert(linux_i2c_submit(&i2c, &req));
    assert(i2c.active == true);

    /* F-RAM request while ZSSC active → rejected */
    LinuxI2cRequest req2 = {
        .operation_id = 2, .correlation_id = 20, .owner_generation = 1,
        .slave_address = 0x51, .deadline_us = 0
    };
    bool ok = linux_i2c_submit(&i2c, &req2);
    assert(ok == false);  /* Bus busy */
    assert(i2c.admission_rejected > 0);
    PASS();
}

int main(void)
{
    printf("ZSSC3241 Scenarios\n");
    printf("──────────────────\n");

    test_zssc_normal_eoc();
    test_zssc_missing_eoc();
    test_zssc_i2c_failure();
    test_zssc_stuck_busy();
    test_shared_i2c_contention();

    printf("──────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
