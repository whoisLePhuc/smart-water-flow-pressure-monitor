/**
 * Numeric utility unit tests
 */

#include "checked_math.h"
#include "interpolation.h"
#include <stdio.h>
#include <assert.h>

static int tests_passed = 0, tests_failed = 0;
#define TEST(name) do { printf("  %-40s ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static void test_checked_add_ok(void)
{
    int64_t r;
    assert(checked_add_i64(5, 3, &r) && r == 8);
    assert(checked_add_i64(-5, 3, &r) && r == -2);
    assert(checked_add_i64(INT64_MAX, 0, &r) && r == INT64_MAX);
    PASS();
}

static void test_checked_add_overflow(void)
{
    int64_t r;
    assert(!checked_add_i64(INT64_MAX, 1, &r));
    assert(!checked_add_i64(INT64_MIN, -1, &r));
    PASS();
}

static void test_checked_mul_ok(void)
{
    int64_t r;
    assert(checked_mul_i64(7, 6, &r) && r == 42);
    assert(checked_mul_i64(-2, 3, &r) && r == -6);
    assert(checked_mul_i64(0, 100, &r) && r == 0);
    PASS();
}

static void test_checked_mul_overflow(void)
{
    int64_t r;
    assert(!checked_mul_i64(INT64_MAX, 2, &r));
    assert(!checked_mul_i64(INT64_MIN, -1, &r));
    PASS();
}

static void test_round_ties_to_even(void)
{
    assert(round_i64(5, 2) == 2);   /* 2.5 → 2 (even) */
    assert(round_i64(7, 2) == 4);   /* 3.5 → 4 (even) */
    assert(round_i64(1, 3) == 0);   /* 0.333 → 0 */
    assert(round_i64(5, 3) == 2);   /* 1.667 → 2 */
    assert(round_i64(-3, 2) == -2); /* -1.5 → -2 (even) */
    PASS();
}

static void test_sat_add(void)
{
    assert(sat_add_i64(INT64_MAX, 1) == INT64_MAX);
    assert(sat_add_i64(INT64_MIN, -1) == INT64_MIN);
    assert(sat_add_i64(10, 5) == 15);
    PASS();
}

static void test_table_monotonic(void)
{
    const int64_t x[] = {0, 100, 200};
    const int64_t y[] = {0, 50, 100};
    assert(table_is_monotonic(x, y, 3));
    assert(!table_is_monotonic(x, y, 0));
    const int64_t x2[] = {0, 100, 100}; /* not strictly increasing */
    assert(!table_is_monotonic(x2, y, 3));
    PASS();
}

static void test_interpolate(void)
{
    const int64_t x[] = {0, 100, 200};
    const int64_t y[] = {0, 50, 100};
    int64_t r;
    assert(interpolate_i64(50, x, y, 3, &r) && r == 25);
    assert(interpolate_i64(0, x, y, 3, &r) && r == 0);   /* exact */
    assert(interpolate_i64(200, x, y, 3, &r) && r == 100); /* exact */
    assert(interpolate_i64(-10, x, y, 3, &r) && r == 0);  /* clamp low */
    assert(interpolate_i64(999, x, y, 3, &r) && r == 100); /* clamp high */
    PASS();
}

int main(void)
{
    printf("Numeric Utility Tests\n");
    printf("─────────────────────\n");
    test_checked_add_ok();
    test_checked_add_overflow();
    test_checked_mul_ok();
    test_checked_mul_overflow();
    test_round_ties_to_even();
    test_sat_add();
    test_table_monotonic();
    test_interpolate();
    printf("─────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
