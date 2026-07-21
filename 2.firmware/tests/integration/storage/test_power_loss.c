#include "protocols/storage/storage_record.h"
#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define TEST(n)   do { printf("  %-45s ", n); } while(0)
#define PASS()    do { printf("PASS\n"); passed++; } while(0)
#define FAIL(m)   do { printf("FAIL: %s\n", m); failed++; } while(0)

typedef struct { uint8_t bytes[512]; } TestStorage;

static void storage_read(const TestStorage *storage, uint16_t address,
                         uint8_t *buffer, uint16_t size)
{
    memcpy(buffer, storage->bytes + address, size);
}

static void storage_write(TestStorage *storage, uint16_t address,
                          const uint8_t *buffer, uint16_t size)
{
    memcpy(storage->bytes + address, buffer, size);
}

static void wslot(TestStorage *storage, uint16_t a, uint32_t seq,
                  uint64_t fwd, uint64_t rev)
{
    uint8_t b[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(b, seq, fwd, rev, 0, 0, 1, 42, 1);
    b[SLOT_VOLUME_SIZE-1] = PERSIST_COMMIT_VALID;
    storage_write(storage, a, b, SLOT_VOLUME_SIZE);
}

static void test_one_slot_corrupt(void)
{
    TestStorage storage = {0};
    wslot(&storage,SLOT_VOLUME_A_ADDR,1,1000,50);
    uint8_t b[SLOT_VOLUME_SIZE]; memset(b,0,sizeof(b)); /* B is corrupt/empty */
    storage_write(&storage,SLOT_VOLUME_B_ADDR,b,SLOT_VOLUME_SIZE);

    uint8_t a[SLOT_VOLUME_SIZE];
    storage_read(&storage,SLOT_VOLUME_A_ADDR,a,sizeof(a));
    storage_read(&storage,SLOT_VOLUME_B_ADDR,b,sizeof(b));
    SlotSelectionResult r = ab_slot_select(a,sizeof(a),b,sizeof(b),PERSIST_RECORD_VOLUME,1,44);
    if (r.selected_slot!=0) { FAIL("expect A"); return; }
    PASS();
}

static void test_both_corrupt(void)
{
    TestStorage storage = {0};
    uint8_t z[SLOT_VOLUME_SIZE]; memset(z,0,sizeof(z));
    storage_write(&storage,SLOT_VOLUME_A_ADDR,z,SLOT_VOLUME_SIZE);
    storage_write(&storage,SLOT_VOLUME_B_ADDR,z,SLOT_VOLUME_SIZE);
    uint8_t a[SLOT_VOLUME_SIZE],b[SLOT_VOLUME_SIZE];
    storage_read(&storage,SLOT_VOLUME_A_ADDR,a,sizeof(a));
    storage_read(&storage,SLOT_VOLUME_B_ADDR,b,sizeof(b));
    SlotSelectionResult r = ab_slot_select(a,sizeof(a),b,sizeof(b),PERSIST_RECORD_VOLUME,1,44);
    if (r.selected_slot!=0xFF) { FAIL("expect none"); return; }
    PASS();
}

static void test_torn_record_rejected(void)
{
    TestStorage storage = {0};
    wslot(&storage,SLOT_VOLUME_A_ADDR,1,1000,0);
    /* Write a record with INVALID commit byte (simulates torn write) */
    uint8_t b[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(b,2,2000,0,0,0,1,43,1);
    /* b already has PERSIST_COMMIT_INVALID from EncodeVolume */
    storage_write(&storage,SLOT_VOLUME_B_ADDR,b,SLOT_VOLUME_SIZE);

    uint8_t a[SLOT_VOLUME_SIZE],rb[SLOT_VOLUME_SIZE];
    storage_read(&storage,SLOT_VOLUME_A_ADDR,a,sizeof(a));
    storage_read(&storage,SLOT_VOLUME_B_ADDR,rb,sizeof(rb));
    /* B is NOT_COMMITTED, A is valid — should select A */
    SlotSelectionResult r = ab_slot_select(a,sizeof(a),rb,sizeof(rb),PERSIST_RECORD_VOLUME,1,44);
    if (r.selected_slot!=0) { FAIL("expect A (B torn)"); return; }
    PASS();
}

static void test_unknown_schema(void)
{
    TestStorage storage = {0};
    wslot(&storage,SLOT_VOLUME_A_ADDR,1,1000,0);
    uint8_t b[SLOT_VOLUME_SIZE];
    StorageRecord_EncodeVolume(b,2,2000,0,0,0,1,43,1);
    b[5]=99; /* unknown schema version */
    b[SLOT_VOLUME_SIZE-1]=PERSIST_COMMIT_VALID;
    storage_write(&storage,SLOT_VOLUME_B_ADDR,b,SLOT_VOLUME_SIZE);

    uint8_t a[SLOT_VOLUME_SIZE],rb[SLOT_VOLUME_SIZE];
    storage_read(&storage,SLOT_VOLUME_A_ADDR,a,sizeof(a));
    storage_read(&storage,SLOT_VOLUME_B_ADDR,rb,sizeof(rb));
    SlotSelectionResult r = ab_slot_select(a,sizeof(a),rb,sizeof(rb),PERSIST_RECORD_VOLUME,1,44);
    /* A should be selected (B has unsupported schema) */
    if (r.selected_slot!=0) { FAIL("expect A (B unknown schema)"); return; }
    PASS();
}

int main(void)
{
    printf("Power-Loss & Corruption Tests\n");
    printf("─────────────────────────────\n");
    test_one_slot_corrupt();
    test_both_corrupt();
    test_torn_record_rejected();
    test_unknown_schema();
    printf("─────────────────────────────\n");
    printf("%d passed, %d failed\n",passed,failed);
    return failed>0;
}
