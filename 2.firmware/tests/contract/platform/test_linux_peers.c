/**
 * Device peer integration tests
 * Tests: MAX, ZSSC, F-RAM peers with provider + driver integration
 */

#include "peers/peer_max35103.h"
#include "peers/peer_zssc3241.h"
#include "peers/peer_fram.h"
#include "providers/linux_spi_provider.h"
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

/* ── MAX peer tests ───────────────────────────────── */

static void test_max_peer_normal_cycle(void)
{
    Max35103Peer peer;
    max_peer_init(&peer);
    assert(peer.state == MAX_PEER_IDLE);

    max_peer_configure(&peer);
    assert(peer.state == MAX_PEER_CONFIGURED);

    uint64_t int_time = max_peer_schedule_cycle(&peer, 1000);
    assert(int_time > 0);
    assert(peer.state == MAX_PEER_CYCLE_PENDING);
    assert(peer.int_active == true);

    max_peer_clear_int(&peer);
    assert(peer.int_active == false);
    PASS();
}

static void test_max_peer_fault_no_completion(void)
{
    Max35103Peer peer;
    max_peer_init(&peer);
    max_peer_set_fault(&peer, MAX_FAULT_NO_COMPLETION);

    uint8_t tx_val = 0x10, rx_val = 0;
    uint64_t lat;
    uint32_t st;
    bool ok = max_peer_plan_spi(&peer, &tx_val, &rx_val, 1, &lat, &st);
    assert(ok == false);  /* No-completion fault */
    PASS();
}

static void test_max_peer_fault_invalid_result(void)
{
    Max35103Peer peer;
    max_peer_init(&peer);
    max_peer_set_fault(&peer, MAX_FAULT_INVALID_RESULT);

    uint8_t tx_arr[2] = {0x10, 0};
    uint8_t rx_arr[2] = {0};
    uint64_t lat;
    uint32_t st;
    max_peer_plan_spi(&peer, tx_arr, rx_arr, 2, &lat, &st);
    assert(rx_arr[1] == 0xFF);  /* Invalid data marker */
    PASS();
}

static void test_max_peer_reset(void)
{
    Max35103Peer peer;
    max_peer_init(&peer);
    uint32_t gen = peer.generation;

    max_peer_reset(&peer, true);
    assert(peer.generation == gen + 1);
    assert(peer.state == MAX_PEER_RESET);
    PASS();
}

/* ── ZSSC peer tests ──────────────────────────────── */

static void test_zssc_peer_normal_cycle(void)
{
    Zssc3241Peer peer;
    zssc_peer_init(&peer);
    assert(peer.state == ZSSC_PEER_SLEEP);

    uint64_t eoc_time = zssc_peer_schedule_eoc(&peer, 5000);
    assert(eoc_time > 5000);
    assert(peer.state == ZSSC_PEER_CONVERTING);
    assert(peer.eoc_active == true);

    zssc_peer_clear_eoc(&peer);
    assert(peer.eoc_active == false);
    PASS();
}

static void test_zssc_peer_fault_missing_eoc(void)
{
    Zssc3241Peer peer;
    zssc_peer_init(&peer);
    zssc_peer_set_fault(&peer, ZSSC_FAULT_MISSING_EOC);

    uint64_t eoc_time = zssc_peer_schedule_eoc(&peer, 1000);
    assert(eoc_time > 1000);
    assert(peer.eoc_active == false);  /* No EOC fired */
    PASS();
}

/* ── F-RAM peer tests ─────────────────────────────── */

static void test_fram_read_write(void)
{
    FramPeer fram;
    fram_peer_init(&fram);

    /* Write data */
    uint8_t tx[] = {0x10, 0xAB, 0xCD};
    uint64_t lat;
    uint32_t st;
    bool ok = fram_peer_plan_i2c(&fram, 0x50, tx, 3, NULL, 0, &lat, &st);
    assert(ok);
    assert(fram.memory[0x10] == 0xAB);
    assert(fram.memory[0x11] == 0xCD);

    /* Read back */
    uint8_t tx_read[] = {0x10};
    uint8_t rx[4] = {0};
    ok = fram_peer_plan_i2c(&fram, 0x50, tx_read, 1, rx, 4, &lat, &st);
    assert(ok);
    assert(rx[0] == 0xAB);
    assert(rx[1] == 0xCD);
    PASS();
}

/* ── Shared-I2C contention ────────────────────────── */

static void test_shared_i2c_registration(void)
{
    LinuxI2cProvider i2c;
    LinuxScheduledActionQueue q;
    action_queue_init(&q);
    linux_i2c_init(&i2c, &q);

    /* Register ZSSC and F-RAM on same bus */
    Zssc3241Peer zssc;
    zssc_peer_init(&zssc);
    LinuxI2cPeer zssc_peer = { .i2c_plan = zssc_peer_plan_i2c, .context = &zssc };
    assert(linux_i2c_register_peer(&i2c, 0x28, zssc_peer));

    FramPeer fram;
    fram_peer_init(&fram);
    LinuxI2cPeer fram_peer = { .i2c_plan = fram_peer_plan_i2c, .context = &fram };
    assert(linux_i2c_register_peer(&i2c, 0x50, fram_peer));
    assert(linux_i2c_register_peer(&i2c, 0x51, fram_peer));

    assert(i2c.peer_count == 3);
    PASS();
}

int main(void)
{
    printf("Device Peer Integration Tests\n");
    printf("──────────────────────────────\n");

    test_max_peer_normal_cycle();
    test_max_peer_fault_no_completion();
    test_max_peer_fault_invalid_result();
    test_max_peer_reset();

    test_zssc_peer_normal_cycle();
    test_zssc_peer_fault_missing_eoc();

    test_fram_read_write();
    test_shared_i2c_registration();

    printf("──────────────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
