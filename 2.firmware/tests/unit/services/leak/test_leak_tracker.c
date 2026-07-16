#include "services/leak/leak_tracker.h"
#include <stdio.h>
#include <string.h>

static int p=0,f=0;
#define T(n) printf("  %-45s ",n)
#define PASS() do{p++;printf("PASS\n");}while(0)
#define FAIL(m) do{f++;printf("FAIL: %s\n",m);}while(0)

static void test_init_inactive(void)
{
    LeakEvidenceTracker tr; LeakTracker_Init(&tr);
    if (tr.phase != LEAK_PHASE_INACTIVE) { FAIL("expect INACTIVE"); return; }
    PASS();
}

static void test_entry_pending(void)
{
    LeakEvidenceTracker tr; LeakTracker_Init(&tr);
    LeakTracker_Evaluate(&tr, true, false, true, 100, 1000, 500, 5000);
    if (tr.phase != LEAK_PHASE_PENDING) { FAIL("expect PENDING"); return; }
    PASS();
}

static void test_activation(void)
{
    LeakEvidenceTracker tr; LeakTracker_Init(&tr);
    LeakTracker_Evaluate(&tr, true, false, true, 0, 1000, 500, 5000);
    LeakTracker_Evaluate(&tr, true, false, true, 1000, 1000, 500, 5000);
    if (tr.phase != LEAK_PHASE_ACTIVE) { FAIL("expect ACTIVE"); return; }
    PASS();
}

static void test_clear_pending(void)
{
    LeakEvidenceTracker tr; LeakTracker_Init(&tr);
    LeakTracker_Evaluate(&tr, true, false, true, 0, 100, 500, 5000);
    LeakTracker_Evaluate(&tr, true, false, true, 100, 100, 500, 5000);
    if (tr.phase != LEAK_PHASE_ACTIVE) { FAIL("expect ACTIVE before clear"); return; }
    LeakTracker_Evaluate(&tr, false, true, true, 200, 100, 500, 5000);
    if (tr.phase != LEAK_PHASE_CLEAR_PENDING) { FAIL("expect CLEAR_PENDING"); return; }
    PASS();
}

static void test_suspend_on_unusable(void)
{
    LeakEvidenceTracker tr; LeakTracker_Init(&tr);
    LeakTracker_Evaluate(&tr, true, false, true, 0, 100, 500, 5000);
    LeakTracker_Evaluate(&tr, true, false, true, 100, 100, 500, 5000);
    if (tr.phase != LEAK_PHASE_ACTIVE) { FAIL("expect ACTIVE"); return; }
    LeakTracker_Evaluate(&tr, true, false, false, 200, 100, 500, 5000);
    if (tr.phase != LEAK_PHASE_SUSPENDED) { FAIL("expect SUSPENDED"); return; }
    PASS();
}

int main(void)
{
    printf("Leak Tracker Tests\n");
    printf("──────────────────\n");
    test_init_inactive();
    test_entry_pending();
    test_activation();
    test_clear_pending();
    test_suspend_on_unusable();
    printf("──────────────────\n");
    printf("%d passed, %d failed\n",p,f);
    return f>0;
}
