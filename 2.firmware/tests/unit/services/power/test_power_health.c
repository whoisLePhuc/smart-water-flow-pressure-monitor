#include "power_service.h"
#include <stdio.h>
#include <assert.h>

static int tests_passed = 0, tests_failed = 0;
#define TEST(n) printf("  TEST: %s ... ", n)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); tests_failed++; } while(0)

/* mV = (raw * 3300 * 3 + 2048) / 4096
 * raw=1600 → 3867 mV (NORMAL)
 * raw=1500 → 3625 mV (LOW, below 3700 hysteresis)
 * raw=1400 → 3384 mV (LOW)
 * raw=1300 → 3142 mV (CRITICAL) */

static void test_health_init_unknown(void)
{
    TEST("init_unknown");
    PowerService svc;
    PowerConfig cfg = POWER_CONFIG_DEFAULT;
    power_service_init(&svc, &cfg);
    assert(power_service_get_health(&svc) == POWER_STATE_UNKNOWN);
    PASS();
}

static void test_health_normal_to_low(void)
{
    TEST("normal_to_low");
    PowerService svc;
    PowerConfig cfg = POWER_CONFIG_DEFAULT;
    power_service_init(&svc, &cfg);
    power_service_sample(&svc, 1600);
    assert(power_service_get_health(&svc) == POWER_STATE_NORMAL);
    power_service_sample(&svc, 1400);
    assert(power_service_get_health(&svc) == POWER_STATE_LOW);
    PASS();
}

static void test_health_low_to_normal_hysteresis(void)
{
    TEST("low_hysteresis");
    PowerService svc;
    PowerConfig cfg = POWER_CONFIG_DEFAULT;
    power_service_init(&svc, &cfg);
    power_service_sample(&svc, 1400);
    assert(power_service_get_health(&svc) == POWER_STATE_LOW);
    power_service_sample(&svc, 1500);
    assert(power_service_get_health(&svc) == POWER_STATE_LOW);
    PASS();
}

static void test_health_low_to_critical(void)
{
    TEST("low_to_critical");
    PowerService svc;
    PowerConfig cfg = POWER_CONFIG_DEFAULT;
    power_service_init(&svc, &cfg);
    power_service_sample(&svc, 1400);
    assert(power_service_get_health(&svc) == POWER_STATE_LOW);
    power_service_sample(&svc, 1300);
    assert(power_service_get_health(&svc) == POWER_STATE_CRITICAL);
    PASS();
}

int main(void)
{
    printf("Power Health Tests\n");
    printf("───────────────────\n");
    test_health_init_unknown();
    test_health_normal_to_low();
    test_health_low_to_normal_hysteresis();
    test_health_low_to_critical();
    printf("───────────────────\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
