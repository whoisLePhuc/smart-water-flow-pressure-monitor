#include "power_converter.h"
#include <stdio.h>
#include <assert.h>

static int tests_passed = 0, tests_failed = 0;
#define TEST(n) printf("  TEST: %s ... ", n)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); tests_failed++; } while(0)

static void test_adc_to_mv_zero(void)
{
    TEST("adc_to_mv_zero");
    int32_t mv = power_adc_to_mv(0, 3300, 3);
    assert(mv == 0);
    PASS();
}

static void test_adc_to_mv_mid(void)
{
    TEST("adc_to_mv_mid");
    int32_t mv = power_adc_to_mv(2048, 3300, 3);
    assert(mv > 4940 && mv < 4960);
    PASS();
}

static void test_adc_to_mv_full(void)
{
    TEST("adc_to_mv_full");
    int32_t mv = power_adc_to_mv(4095, 3300, 3);
    assert(mv > 9880 && mv < 9900);
    PASS();
}

static void test_adc_to_mv_divider_one(void)
{
    TEST("adc_to_mv_no_divider");
    int32_t mv = power_adc_to_mv(2048, 3300, 1);
    assert(mv > 1640 && mv < 1660);
    PASS();
}

static void test_adc_to_mv_zero_divider(void)
{
    TEST("adc_to_mv_zero_divider");
    int32_t mv = power_adc_to_mv(2048, 3300, 0);
    assert(mv == -1);
    PASS();
}

int main(void)
{
    printf("Power Converter Tests\n");
    printf("─────────────────────\n");
    test_adc_to_mv_zero();
    test_adc_to_mv_mid();
    test_adc_to_mv_full();
    test_adc_to_mv_divider_one();
    test_adc_to_mv_zero_divider();
    printf("─────────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
