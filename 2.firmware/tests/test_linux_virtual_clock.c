/**
 * Linux Virtual Clock unit tests
 */

#include "platform/include/linux_virtual_clock.h"
#include <stdio.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  TEST: %s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static void test_now_after_advance(void)
{
    LinuxVirtualClock clk;
    linux_clock_init(&clk, LINUX_CLOCK_MODE_DETERMINISTIC);
    assert(linux_clock_now_us(&clk) == 0);

    linux_clock_advance_by(&clk, 100);
    assert(linux_clock_now_us(&clk) == 100);

    linux_clock_advance_by(&clk, 50);
    assert(linux_clock_now_us(&clk) == 150);
    PASS();
}

static void test_advance_to_rejects_backward(void)
{
    LinuxVirtualClock clk;
    linux_clock_init(&clk, LINUX_CLOCK_MODE_DETERMINISTIC);
    linux_clock_advance_by(&clk, 500);

    bool r = linux_clock_advance_to(&clk, 100);  /* < now */
    assert(r == false);
    assert(linux_clock_now_us(&clk) == 500);  /* unchanged */
    PASS();
}

static void test_advance_to_forward(void)
{
    LinuxVirtualClock clk;
    linux_clock_init(&clk, LINUX_CLOCK_MODE_DETERMINISTIC);
    linux_clock_advance_by(&clk, 100);

    bool r = linux_clock_advance_to(&clk, 500);
    assert(r == true);
    assert(linux_clock_now_us(&clk) == 500);
    PASS();
}

static void test_advance_to_same_time(void)
{
    LinuxVirtualClock clk;
    linux_clock_init(&clk, LINUX_CLOCK_MODE_DETERMINISTIC);
    linux_clock_advance_by(&clk, 200);

    bool r = linux_clock_advance_to(&clk, 200);
    assert(r == true);
    assert(linux_clock_now_us(&clk) == 200);
    PASS();
}

static void test_wall_clock_independent(void)
{
    LinuxVirtualClock clk;
    linux_clock_init(&clk, LINUX_CLOCK_MODE_DETERMINISTIC);

    linux_clock_set_wall(&clk, 1000, 0, true);
    assert(clk.wall_base_s == 1000);
    assert(clk.time_generation == 1);

    /* Wall step must NOT affect monotonic time */
    assert(linux_clock_now_us(&clk) == 0);

    /* Monotonic advance must NOT affect wall */
    linux_clock_advance_by(&clk, 999);
    assert(clk.wall_base_s == 1000);
    PASS();
}

static void test_reset_creates_new_generation(void)
{
    LinuxVirtualClock clk;
    linux_clock_init(&clk, LINUX_CLOCK_MODE_DETERMINISTIC);
    uint32_t gen1 = clk.boot_generation;

    linux_clock_advance_by(&clk, 5000);
    assert(linux_clock_now_us(&clk) == 5000);

    linux_clock_reset(&clk, false);
    assert(clk.boot_generation == gen1 + 1);
    assert(linux_clock_now_us(&clk) == 0);
    assert(clk.wall_time_valid == false);
    PASS();
}

static void test_reset_preserves_wall(void)
{
    LinuxVirtualClock clk;
    linux_clock_init(&clk, LINUX_CLOCK_MODE_DETERMINISTIC);
    linux_clock_set_wall(&clk, 50000, 0, true);

    linux_clock_reset(&clk, true);
    assert(clk.wall_base_s == 50000);
    assert(clk.wall_time_valid == true);
    PASS();
}

int main(void)
{
    printf("Linux Virtual Clock Tests\n");
    printf("─────────────────────────\n");

    test_now_after_advance();
    test_advance_to_rejects_backward();
    test_advance_to_forward();
    test_advance_to_same_time();
    test_wall_clock_independent();
    test_reset_creates_new_generation();
    test_reset_preserves_wall();

    printf("─────────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
