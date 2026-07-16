#include "services/time/time_service.h"
#include <stdio.h>
#include <string.h>

static int p=0,f=0;
#define T(n) printf("  %-45s ",n)
#define PASS() do{p++;printf("PASS\n");}while(0)
#define FAIL(m) do{f++;printf("FAIL: %s\n",m);}while(0)

static void test_init_invalid(void)
{
    TimeService svc;
    TimeService_Init(&svc, 0);
    if (svc.valid) { FAIL("should start invalid"); return; }
    if (svc.quality != SYS_TIME_INVALID) { FAIL("quality INVALID"); return; }
    PASS();
}

static void test_set_valid(void)
{
    TimeService svc;
    TimeService_Init(&svc, 0);
    bool ok = TimeService_SetWallTime(&svc, 1000000, SYS_TIME_NETWORK_SYNCED, 2, 0);
    if (!ok) { FAIL("set failed"); return; }
    if (!svc.valid) { FAIL("should be valid"); return; }
    if (svc.quality != SYS_TIME_NETWORK_SYNCED) { FAIL("quality wrong"); return; }
    if (svc.time_generation != 2) { FAIL("generation should increment"); return; }
    PASS();
}

static void test_holdover_expiry(void)
{
    TimeService svc;
    TimeService_Init(&svc, 0);
    TimeService_SetWallTime(&svc, 1000000, SYS_TIME_NETWORK_SYNCED, 2, 0);

    /* Tick before expiry — should stay valid */
    TimeService_Tick(&svc, 604799000000ULL); /* just under 7 days */
    if (!svc.valid) { FAIL("should still be valid before expiry"); return; }

    /* Tick at expiry */
    TimeService_Tick(&svc, 604800000000ULL);
    if (svc.valid) { FAIL("should expire at max age"); return; }
    if (svc.quality != SYS_TIME_INVALID) { FAIL("quality INVALID after expiry"); return; }
    PASS();
}

static void test_holdover_rtc_valid(void)
{
    TimeService svc;
    TimeService_Init(&svc, 0);
    TimeService_SetWallTime(&svc, 1000000, SYS_TIME_RTC_HOLDOVER, 1, 0);
    if (!svc.valid) { FAIL("RTC holdover should be valid"); return; }
    PASS();
}

static void test_reject_negative_time(void)
{
    TimeService svc;
    TimeService_Init(&svc, 0);
    bool ok = TimeService_SetWallTime(&svc, -1, SYS_TIME_NETWORK_SYNCED, 2, 0);
    if (ok) { FAIL("should reject negative time"); return; }
    PASS();
}

int main(void)
{
    printf("Time Service Tests\n");
    printf("──────────────────\n");
    test_init_invalid();
    test_set_valid();
    test_holdover_expiry();
    test_holdover_rtc_valid();
    test_reject_negative_time();
    printf("──────────────────\n");
    printf("%d passed, %d failed\n", p, f);
    return f>0;
}
