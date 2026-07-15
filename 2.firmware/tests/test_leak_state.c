#include "services/leak_detection.h"
#include "services/leak_config.h"
#include <stdio.h>
#include <string.h>

static int p=0,f=0;
#define T(n) printf("  %-45s ",n)
#define PASS() do{p++;printf("PASS\n");}while(0)
#define FAIL(m) do{f++;printf("FAIL: %s\n",m);}while(0)

static LeakDetectionConfig cfg;
static LeakInputView make_input(int64_t flow, uint32_t seq, uint64_t t_us, bool usable)
{
    LeakInputView v; memset(&v,0,sizeof(v));
    v.evaluation_monotonic_us = t_us;
    v.sample_sequence = seq;
    v.source_generation = 1;
    v.flow_usable = usable;
    v.flow_ul_per_s = flow;
    v.flow_direction = (flow > 0) ? FLOW_DIRECTION_FORWARD :
                       (flow < 0) ? FLOW_DIRECTION_REVERSE : FLOW_DIRECTION_NONE;
    return v;
}

static void test_init_state(void)
{
    LeakDetectionService svc;
    LeakDetection_Init(&svc, &cfg, 0);
    if (svc.state != LEAK_STATE_NORMAL) { FAIL("expect NORMAL"); return; }
    if (svc.eval_status != LEAK_EVAL_NOT_READY) { FAIL("expect NOT_READY"); return; }
    PASS();
}

static void test_normal_to_suspected(void)
{
    LeakDetectionService svc;
    LeakDetection_Init(&svc, &cfg, 0);
    /* Wait past suspect duration */
    LeakInputView v = make_input(cfg.continuous_entry_ul_per_s, 1, 0, true);
    LeakDetection_Evaluate(&svc, &v);
    v.sample_sequence = 2;
    v.evaluation_monotonic_us = cfg.continuous_suspect_duration_us + 1;
    LeakDetection_Evaluate(&svc, &v);
    if (svc.state != LEAK_STATE_SUSPECTED) { FAIL("expect SUSPECTED"); return; }
    PASS();
}

static void test_normal_to_confirmed_burst(void)
{
    LeakDetectionService svc;
    LeakDetection_Init(&svc, &cfg, 0);
    LeakInputView v = make_input(cfg.burst_entry_ul_per_s, 1, 0, true);
    LeakDetection_Evaluate(&svc, &v);
    v.sample_sequence = 2;
    v.evaluation_monotonic_us = cfg.burst_confirm_duration_us + 1;
    LeakDetection_Evaluate(&svc, &v);
    if (svc.state != LEAK_STATE_CONFIRMED) { FAIL("expect CONFIRMED"); return; }
    if (svc.primary_reason != LEAK_REASON_HIGH_FLOW_BURST) { FAIL("expect BURST reason"); return; }
    PASS();
}

static void test_pressure_only_no_leak(void)
{
    LeakDetectionService svc;
    LeakDetection_Init(&svc, &cfg, 0);
    LeakInputView v; memset(&v,0,sizeof(v));
    v.evaluation_monotonic_us = 0;
    v.sample_sequence = 1;
    v.flow_usable = true;
    v.flow_ul_per_s = 0;
    v.flow_direction = FLOW_DIRECTION_NONE;
    v.pressure_usable = true;
    v.pressure_pa = 50000; /* below low threshold */
    LeakDetection_Evaluate(&svc, &v);
    if (svc.state != LEAK_STATE_NORMAL) { FAIL("pressure only should stay NORMAL"); return; }
    PASS();
}

int main(void)
{
    LeakConfig_GetTestDefaults(&cfg);
    printf("Leak State Machine Tests\n");
    printf("────────────────────────\n");
    test_init_state();
    test_normal_to_suspected();
    test_normal_to_confirmed_burst();
    test_pressure_only_no_leak();
    printf("────────────────────────\n");
    printf("%d passed, %d failed\n",p,f);
    return f>0;
}
