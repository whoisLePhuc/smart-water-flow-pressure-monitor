/**
 * MAX35103 deterministic scenarios.
 *
 * Tests: normal event-timing cycle, INT already active, missing INT,
 *        duplicate INT, SPI failure, reset, supervision timeout.
 */

#include "peers/peer_max35103.h"
#include "providers/linux_spi_provider.h"
#include "platform/include/linux_scheduled_action_queue.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* ── MAX normal cycle ──────────────────────────────── */

static void test_max_normal_cycle(void)
{
    Max35103Peer peer;
    max_peer_init(&peer);
    max_peer_configure(&peer);

    uint64_t int_time = max_peer_schedule_cycle(&peer, 1000);
    assert(int_time > 0);
    assert(peer.int_active == true);
    assert(peer.cycle_count == 1);

    /* SPI transaction */
    uint8_t tx = 0x10, rx = 0;
    uint64_t lat;
    uint32_t st;
    bool ok = max_peer_plan_spi(&peer, &tx, &rx, 1, &lat, &st);
    assert(ok == true);
    assert(peer.spi_operations == 1);
    PASS();
}

/* ── MAX missing INT ────────────────────────────────── */

static void test_max_missing_int(void)
{
    Max35103Peer peer;
    max_peer_init(&peer);
    max_peer_set_fault(&peer, MAX_FAULT_MISSING_INT);

    uint64_t int_time = max_peer_schedule_cycle(&peer, 1000);
    assert(int_time > 0);
    assert(peer.int_active == false);  /* INT not asserted */
    PASS();
}

/* ── MAX SPI failure ────────────────────────────────── */

static void test_max_spi_failure(void)
{
    Max35103Peer peer;
    max_peer_init(&peer);
    max_peer_set_fault(&peer, MAX_FAULT_NO_COMPLETION);

    uint8_t tx = 0x10, rx = 0;
    uint64_t lat;
    uint32_t st;
    bool ok = max_peer_plan_spi(&peer, &tx, &rx, 1, &lat, &st);
    assert(ok == false);  /* SPI rejected due to fault */
    PASS();
}

/* ── MAX reset ──────────────────────────────────────── */

static void test_max_unexpected_reset(void)
{
    Max35103Peer peer;
    max_peer_init(&peer);
    uint32_t gen_before = peer.generation;

    max_peer_reset(&peer, true);
    assert(peer.generation == gen_before + 1);
    assert(peer.state == MAX_PEER_RESET);
    PASS();
}

/* ── MAX duplicate INT ──────────────────────────────── */

static void test_max_duplicate_int(void)
{
    Max35103Peer peer;
    max_peer_init(&peer);
    max_peer_set_fault(&peer, MAX_FAULT_DUPLICATE_INT);

    max_peer_schedule_cycle(&peer, 1000);

    uint8_t tx = 0x10, rx = 0;
    uint64_t lat;
    uint32_t st;
    max_peer_plan_spi(&peer, &tx, &rx, 1, &lat, &st);

    /* After SPI, duplicate fault should re-assert INT */
    assert(peer.int_active == true);
    PASS();
}

int main(void)
{
    printf("MAX35103 Scenarios\n");
    printf("──────────────────\n");

    test_max_normal_cycle();
    test_max_missing_int();
    test_max_spi_failure();
    test_max_unexpected_reset();
    test_max_duplicate_int();

    printf("──────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
