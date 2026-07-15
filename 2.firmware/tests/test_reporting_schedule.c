#include "services/reporting_schedule.h"
#include <stdio.h>
#include <string.h>

static int p=0,f=0;
#define T(n) printf("  %-45s ",n)
#define PASS() do{p++;printf("PASS\n");}while(0)
#define FAIL(m) do{f++;printf("FAIL: %s\n",m);}while(0)

static int64_t day0(void) { return (1000000LL / 86400LL) * 86400LL; }

static void test_window_boundary(void)
{
    ReportingSchedule rs; ReportingSchedule_Init(&rs);
    int64_t d0 = day0();
    if (RS_GetActiveWindow(&rs.cfg, d0 + 5*3600+59*60) != 1) { FAIL("0559 W1"); return; }
    if (RS_GetActiveWindow(&rs.cfg, d0 + 6*3600) != 0)        { FAIL("0600 W0"); return; }
    if (RS_GetActiveWindow(&rs.cfg, d0 + 21*3600+59*60) != 0) { FAIL("2159 W0"); return; }
    if (RS_GetActiveWindow(&rs.cfg, d0 + 22*3600) != 1)       { FAIL("2200 W1"); return; }
    PASS();
}

static void test_next_due(void)
{
    ReportingSchedule rs; ReportingSchedule_Init(&rs);
    int64_t d0 = day0();
    int64_t n = RS_GetNextDue(&rs.cfg, d0 + 5*3600+59*60);
    if (n != d0 + 6*3600) { FAIL("next 0600"); return; }
    n = RS_GetNextDue(&rs.cfg, d0 + 21*3600+59*60);
    if (n != d0 + 22*3600) { FAIL("next 2200"); return; }
    PASS();
}

static void test_slot_count(void)
{
    ReportingSchedule rs; ReportingSchedule_Init(&rs);
    int64_t d0 = day0();
    int64_t slots[200]; int cnt=0;
    for (int64_t t = d0; t < d0+86400; t+=60) {
        if (cnt>=200) break;
        uint8_t w = RS_GetActiveWindow(&rs.cfg, t);
        uint16_t ord = RS_GetSlotOrdinal(&rs.cfg, w, t);
        int64_t due = RS_GetSlotDueWall(&rs.cfg, w, ord);
        if (cnt==0 || due!=slots[cnt-1]) slots[cnt++] = due;
    }
    if (cnt != 160) { FAIL("expect 160 slots"); return; }
    PASS();
}

static void test_config_valid(void)
{
    ReportingScheduleConfig cfg;
    memset(&cfg,0,sizeof(cfg)); cfg.schedule_version=1;
    cfg.windows[0].start_minute=360; cfg.windows[0].interval_minutes=15;
    cfg.windows[1].start_minute=1320; cfg.windows[1].interval_minutes=5;
    if (!RS_ValidateConfig(&cfg,0,0)) { FAIL("valid rej"); return; }
    PASS();
}

static void test_config_invalid(void)
{
    ReportingScheduleConfig cfg;
    memset(&cfg,0,sizeof(cfg)); cfg.schedule_version=1;
    cfg.windows[0].start_minute=360; cfg.windows[0].interval_minutes=3;
    cfg.windows[1].start_minute=1320; cfg.windows[1].interval_minutes=70;
    if (RS_ValidateConfig(&cfg,0,0)) { FAIL("invalid accept"); return; }
    PASS();
}

static void test_dedup(void)
{
    ReportingSchedule rs; ReportingSchedule_Init(&rs);
    ReportSlotIdentity s = {1,0,1000000};
    if (RS_IsSlotAccepted(&rs,&s)) { FAIL("pre"); return; }
    RS_MarkSlotAccepted(&rs,&s);
    if (!RS_IsSlotAccepted(&rs,&s)) { FAIL("post"); return; }
    PASS();
}

int main(void)
{
    printf("Reporting Schedule Tests\n");
    printf("────────────────────────\n");
    test_window_boundary();
    test_next_due();
    test_slot_count();
    test_config_valid();
    test_config_invalid();
    test_dedup();
    printf("────────────────────────\n");
    printf("%d passed, %d failed\n",p,f);
    return f>0;
}
