#include "services/storage_record.h"
#include "services/fram_driver.h"
#include "services/volume_accumulator.h"
#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define TEST(n)   do { printf("  %-45s ", n); } while(0)
#define PASS()    do { printf("PASS\n"); passed++; } while(0)
#define FAIL(m)   do { printf("FAIL: %s\n", m); failed++; } while(0)

static VolumeConfig tcfg = {1,5000000,100000,3600,60};

static void wslot(FramDriver *f, uint16_t a, uint32_t seq, uint64_t fwd, uint64_t rev)
{
    uint8_t b[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(b, seq, fwd, rev, 0, 0, 1, 42, 1);
    b[SLOT_VOLUME_SIZE-1] = PERSIST_COMMIT_VALID;
    FramDriver_Write(f, a, b, SLOT_VOLUME_SIZE);
}

static void test_fresh(void)
{
    FramDriver f; FramDriver_Init(&f,0,0,0);
    uint8_t a[SLOT_VOLUME_SIZE],b[SLOT_VOLUME_SIZE];
    memset(a,0,sizeof(a)); memset(b,0,sizeof(b));
    SlotSelectionResult r = ab_slot_select(a,sizeof(a),b,sizeof(b),PERSIST_RECORD_VOLUME,1,44);
    if (r.selected_slot != 0xFF) { FAIL("no slot"); return; }
    PASS();
}

static void test_slot_a(void)
{
    FramDriver f; FramDriver_Init(&f,0,0,0);
    wslot(&f,SLOT_VOLUME_A_ADDR,1,5000,200);
    uint8_t b[SLOT_VOLUME_SIZE]; FramDriver_Read(&f,SLOT_VOLUME_A_ADDR,b,sizeof(b));
    if (StorageRecord_ClassifySlot(b,sizeof(b),PERSIST_RECORD_VOLUME,1,44)!=SLOT_VALID_COMPATIBLE)
        { FAIL("valid"); return; }
    uint64_t fwd,rev; StorageRecord_DecodeVolume(b,&fwd,&rev,0,0,0,0,0);
    if (fwd!=5000) { FAIL("fwd 5000"); return; }
    if (rev!=200)  { FAIL("rev 200"); return; }
    PASS();
}

static void test_newest(void)
{
    FramDriver f; FramDriver_Init(&f,0,0,0);
    wslot(&f,SLOT_VOLUME_A_ADDR,1,1000,0);
    wslot(&f,SLOT_VOLUME_B_ADDR,2,2000,0);
    uint8_t a[SLOT_VOLUME_SIZE],b[SLOT_VOLUME_SIZE];
    FramDriver_Read(&f,SLOT_VOLUME_A_ADDR,a,sizeof(a));
    FramDriver_Read(&f,SLOT_VOLUME_B_ADDR,b,sizeof(b));
    SlotSelectionResult r = ab_slot_select(a,sizeof(a),b,sizeof(b),PERSIST_RECORD_VOLUME,1,44);
    if (r.selected_slot!=1) { FAIL("expect B"); return; }
    uint64_t fwd; StorageRecord_DecodeVolume(r.selected_slot?b:a,&fwd,0,0,0,0,0,0);
    if (fwd!=2000) { FAIL("fwd 2000"); return; }
    PASS();
}

static void test_anchor(void)
{
    VolumeAccumulator v; VolumeAccumulator_Init(&v,&tcfg);
    VolumeState s; memset(&s,0,sizeof(s)); s.forward_volume_ul=5000; s.state_version=10;
    VolumeAccumulator_Restore(&v,&s);
    if (VolumeAccumulator_GetState(&v)->forward_volume_ul!=5000) { FAIL("fwd"); return; }

    FlowResult f; memset(&f,0,sizeof(f));
    f.meta.purpose=MEAS_PURPOSE_PRODUCTION; f.meta.origin=DATA_ORIGIN_LIVE_DEVICE;
    f.meta.provenance=PROVENANCE_MEASURED; f.meta.validity=DATA_VALID;
    f.meta.freshness=DATA_FRESH; f.meta.acceptance=DATA_ACCEPTED;
    f.meta.source_generation=1; f.meta.sample_sequence=1; f.meta.result_version=1;
    f.meta.sample_monotonic_us=1000000; f.meta.binding.binding_id=1;
    f.flow_ul_per_s=1000;
    if (VolumeAccumulator_Consume(&v,&f)!=VOLUME_ANCHORED) { FAIL("anchor"); return; }
    PASS();
}

int main(void)
{
    printf("Boot Restore Tests\n");
    printf("──────────────────\n");
    test_fresh(); test_slot_a(); test_newest(); test_anchor();
    printf("──────────────────\n");
    printf("%d passed, %d failed\n",passed,failed);
    return failed>0;
}
