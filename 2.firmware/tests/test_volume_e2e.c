#include "services/volume_accumulator.h"
#include "services/storage_record.h"
#include "services/fram_driver.h"
#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define TEST(n)   do { printf("  %-45s ", n); } while(0)
#define PASS()    do { printf("PASS\n"); passed++; } while(0)
#define FAIL(m)   do { printf("FAIL: %s\n", m); failed++; } while(0)

static VolumeConfig tcfg = {1,5000000,100000,3600,60};

static FlowResult mkflow(int64_t flow, uint64_t t, uint64_t seq, uint32_t gen)
{
    FlowResult f; memset(&f,0,sizeof(f));
    f.meta.purpose=MEAS_PURPOSE_PRODUCTION; f.meta.origin=DATA_ORIGIN_LIVE_DEVICE;
    f.meta.provenance=PROVENANCE_MEASURED; f.meta.validity=DATA_VALID;
    f.meta.freshness=DATA_FRESH; f.meta.acceptance=DATA_ACCEPTED;
    f.meta.source_generation=gen; f.meta.sample_sequence=seq;
    f.meta.result_version=1; f.meta.sample_monotonic_us=t;
    f.meta.binding.binding_id=1; f.flow_ul_per_s=flow;
    return f;
}

static void test_accumulate_then_encode_then_decode(void)
{
    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &tcfg);

    FlowResult f1 = mkflow(1000, 1000000, 1, 1);
    if (VolumeAccumulator_Consume(&acc, &f1) != VOLUME_ANCHORED)
        { FAIL("anchor1"); return; }
    FlowResult f2 = mkflow(1000, 2000000, 2, 1);
    if (VolumeAccumulator_Consume(&acc, &f2) != VOLUME_OK)
        { FAIL("consume2"); return; }
    FlowResult f3 = mkflow(1000, 3000000, 3, 1);
    if (VolumeAccumulator_Consume(&acc, &f3) != VOLUME_OK)
        { FAIL("consume3"); return; }

    const VolumeState *st = VolumeAccumulator_GetState(&acc);
    if (st->forward_volume_ul != 2000)
        { FAIL("expected 2000 uL forward"); return; }

    CheckpointCandidate cand;
    if (!VolumeAccumulator_PrepareCheckpoint(&acc, &cand))
        { FAIL("prepare"); return; }

    uint8_t buf[SLOT_VOLUME_SIZE];
    uint16_t elen = StorageRecord_EncodeVolume(buf, 1,
        cand.forward_volume_ul, cand.reverse_volume_ul,
        cand.forward_remainder, cand.reverse_remainder,
        cand.state_version, cand.last_consumed_flow_sequence,
        cand.source_generation);
    if (elen != SLOT_VOLUME_SIZE)
        { FAIL("encode"); return; }

    /* Write to FRAM */
    FramDriver fram;
    FramDriver_Init(&fram, 0, 0, 0);
    if (FramDriver_Write(&fram, SLOT_VOLUME_A_ADDR, buf, SLOT_VOLUME_SIZE) != FRAM_DRV_OK)
        { FAIL("fram_write"); return; }

    /* Mark committed */
    uint8_t valid = PERSIST_COMMIT_VALID;
    if (FramDriver_Write(&fram, SLOT_VOLUME_A_ADDR + SLOT_VOLUME_SIZE - 1, &valid, 1) != FRAM_DRV_OK)
        { FAIL("commit"); return; }

    /* Read back and decode */
    uint8_t rb[SLOT_VOLUME_SIZE];
    FramDriver_Read(&fram, SLOT_VOLUME_A_ADDR, rb, SLOT_VOLUME_SIZE);

    uint64_t fwd, rev;
    if (!StorageRecord_DecodeVolume(rb, &fwd, &rev, 0,0,0,0,0))
        { FAIL("decode"); return; }
    if (fwd != 2000 || rev != 0)
        { FAIL("fwd/rev mismatch"); return; }

    PASS();
}

static void test_restore_from_fram(void)
{
    FramDriver fram;
    FramDriver_Init(&fram, 0, 0, 0);

    uint8_t buf[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(buf, 1, 5000, 200, 10, 5, 2, 42, 1);
    buf[SLOT_VOLUME_SIZE-1] = PERSIST_COMMIT_VALID;
    FramDriver_Write(&fram, SLOT_VOLUME_A_ADDR, buf, SLOT_VOLUME_SIZE);

    uint8_t a[SLOT_VOLUME_SIZE], b[SLOT_VOLUME_SIZE];
    FramDriver_Read(&fram, SLOT_VOLUME_A_ADDR, a, sizeof(a));
    memset(b, 0, sizeof(b));

    SlotSelectionResult sel = ab_slot_select(a, sizeof(a), b, sizeof(b),
        PERSIST_RECORD_VOLUME, 1, 44);
    if (sel.selected_slot != 0)
        { FAIL("expect A"); return; }

    uint64_t fwd, rev, frm, rrm, sv, lfs; uint32_t lsg;
    const uint8_t *slot = a;
    StorageRecord_DecodeVolume(slot, &fwd, &rev, &frm, &rrm, &sv, &lfs, &lsg);

    if (fwd != 5000) { FAIL("fwd 5000"); return; }
    if (rev != 200)  { FAIL("rev 200"); return; }
    if (frm != 10)   { FAIL("fwd_rem 10"); return; }
    if (rrm != 5)    { FAIL("rev_rem 5"); return; }

    VolumeAccumulator acc;
    VolumeAccumulator_Init(&acc, &tcfg);

    VolumeState restored;
    memset(&restored, 0, sizeof(restored));
    restored.forward_volume_ul = fwd;
    restored.reverse_volume_ul = rev;
    restored.forward_remainder = frm;
    restored.reverse_remainder = rrm;
    restored.state_version = sv;
    VolumeAccumulator_Restore(&acc, &restored);

    const VolumeState *st = VolumeAccumulator_GetState(&acc);
    if (st->forward_volume_ul != 5000)
        { FAIL("restored fwd"); return; }
    if (st->reverse_volume_ul != 200)
        { FAIL("restored rev"); return; }

    PASS();
}

int main(void)
{
    printf("Volume E2E Pipeline Tests\n");
    printf("─────────────────────────\n");
    test_accumulate_then_encode_then_decode();
    test_restore_from_fram();
    printf("─────────────────────────\n");
    printf("%d passed, %d failed\n", passed, failed);
    return failed > 0;
}
