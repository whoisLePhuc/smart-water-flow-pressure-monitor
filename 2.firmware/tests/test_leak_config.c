#include "services/leak_config.h"
#include <stdio.h>
#include <string.h>

static int p=0,f=0;
#define T(n) printf("  %-45s ",n)
#define PASS() do{p++;printf("PASS\n");}while(0)
#define FAIL(m) do{f++;printf("FAIL: %s\n",m);}while(0)

static void test_defaults_valid(void)
{
    LeakDetectionConfig cfg;
    LeakConfig_GetTestDefaults(&cfg);
    if (!LeakConfig_Validate(&cfg,0,0)) { FAIL("defaults should validate"); return; }
    PASS();
}

static void test_bad_continuous_clear(void)
{
    LeakDetectionConfig cfg; LeakConfig_GetTestDefaults(&cfg);
    cfg.continuous_clear_ul_per_s = cfg.continuous_entry_ul_per_s;
    if (LeakConfig_Validate(&cfg,0,0)) { FAIL("should reject clear>=entry"); return; }
    PASS();
}

static void test_null_rejected(void)
{
    if (LeakConfig_Validate(0,0,0)) { FAIL("null should reject"); return; }
    PASS();
}

static void test_zero_duration_rejected(void)
{
    LeakDetectionConfig cfg; LeakConfig_GetTestDefaults(&cfg);
    cfg.continuous_suspect_duration_us = 0;
    if (LeakConfig_Validate(&cfg,0,0)) { FAIL("zero duration reject"); return; }
    PASS();
}

int main(void)
{
    printf("Leak Config Tests\n");
    printf("─────────────────\n");
    test_defaults_valid();
    test_bad_continuous_clear();
    test_null_rejected();
    test_zero_duration_rejected();
    printf("─────────────────\n");
    printf("%d passed, %d failed\n",p,f);
    return f>0;
}
