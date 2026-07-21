#include "drivers/storage/fram_driver.h"
#include "services/storage/storage_service.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t memory[FM24CL04B_SIZE_BYTES];
    I2cPortRequest request;
    uint32_t transaction_count;
    uint32_t fail_transaction;
    bool active;
} FakeWire;

static PortStatus wire_submit(void *context,
                              const I2cPortRequest *request)
{
    FakeWire *wire = context;
    if (wire->active)
        return PORT_STATUS_BUSY;
    wire->request = *request;
    wire->active = true;
    wire->transaction_count++;
    return PORT_OK;
}

static PortStatus wire_cancel(void *context, uint32_t transaction_id,
                              uint32_t bus_generation)
{
    FakeWire *wire = context;
    if (!wire->active || wire->request.transaction_id != transaction_id ||
        wire->request.bus_generation != bus_generation)
        return PORT_STATUS_INVALID_PARAM;
    wire->active = false;
    return PORT_OK;
}

static PortStatus wire_recover(void *context, uint32_t new_bus_generation)
{
    FakeWire *wire = context;
    (void)new_bus_generation;
    wire->active = false;
    return PORT_OK;
}

static void wire_complete(FakeWire *wire, I2cBusManager *bus)
{
    assert(wire->active);
    I2cPortRequest request = wire->request;
    PortStatus result = wire->fail_transaction == wire->transaction_count
        ? PORT_STATUS_HARDWARE_ERROR : PORT_OK;
    if (result == PORT_OK) {
        assert(request.tx && request.tx_length >= 1u);
        uint16_t address = (uint16_t)(
            ((uint16_t)(request.slave_address & 0x01u) << 8u) |
            request.tx[0]);
        if (request.tx_length > 1u) {
            uint16_t length = (uint16_t)(request.tx_length - 1u);
            memcpy(wire->memory + address, request.tx + 1u, length);
        }
        if (request.rx_length > 0u)
            memcpy(request.rx, wire->memory + address, request.rx_length);
    }
    wire->active = false;
    assert(i2c_bus_on_port_completion(bus, &request, result));
}

static void run_until_terminal(StorageService *service,
                               FakeWire *wire,
                               I2cBusManager *bus,
                               uint64_t *now_us)
{
    for (unsigned i = 0u; i < 256u; ++i) {
        StorageServiceState state = service->context.state;
        if (state == STORAGE_STATE_COMPLETE ||
            state == STORAGE_STATE_FAILED ||
            state == STORAGE_STATE_RESTORE_COMPLETE ||
            state == STORAGE_STATE_RESTORE_FAILED)
            return;
        StorageService_Tick(service, *now_us);
        if (wire->active)
            wire_complete(wire, bus);
        *now_us += 100u;
    }
    assert(false && "F-RAM storage pipeline did not reach terminal state");
}

int main(void)
{
    FakeWire wire;
    memset(&wire, 0, sizeof(wire));
    I2cPort physical = {
        .context = &wire,
        .submit = wire_submit,
        .cancel = wire_cancel,
        .recover = wire_recover
    };
    I2cBusManager bus;
    i2c_bus_init(&bus, &physical);

    FramConfig config = {
        .client_id = 2u,
        .slave_address_base_7bit = 0x50u,
        .capacity_bytes = FM24CL04B_SIZE_BYTES,
        .max_chunk_bytes = 32u,
        .bus_priority = 4u
    };
    FramDriver fram;
    assert(fram_init(&fram, &bus, &config) ==
           STORAGE_IO_SUBMIT_ACCEPTED);
    StoragePort storage_port;
    assert(fram_make_storage_port(&fram, &storage_port));
    StorageService service;
    assert(StorageService_Init(&service, &storage_port, 50000u) ==
           STORAGE_OK);

    uint64_t now_us = 1000u;
    uint8_t record[SLOT_VOLUME_SIZE];
    assert(StorageRecord_EncodeVolume(record, 1u, 1000u, 10u, 1u, 2u,
                                      1u, 10u, 1u) == SLOT_VOLUME_SIZE);
    assert(StorageService_SubmitCheckpoint(
        &service, PERSIST_RECORD_VOLUME, 1u, record,
        SLOT_VOLUME_SIZE, 1u) == STORAGE_OK);
    run_until_terminal(&service, &wire, &bus, &now_us);
    assert(service.context.state == STORAGE_STATE_COMPLETE);
    assert(wire.memory[SLOT_VOLUME_A_ADDR + SLOT_VOLUME_SIZE - 1u] ==
           PERSIST_COMMIT_VALID);
    StorageCompletionPayload completion;
    assert(StorageService_TakeCompletion(&service, &completion));
    assert(completion.status == STORAGE_COMMIT_OK);

    uint32_t before_failed_commit = wire.transaction_count;
    wire.fail_transaction = before_failed_commit + 5u;
    assert(StorageRecord_EncodeVolume(record, 2u, 2000u, 20u, 3u, 4u,
                                      2u, 20u, 1u) == SLOT_VOLUME_SIZE);
    assert(StorageService_SubmitCheckpoint(
        &service, PERSIST_RECORD_VOLUME, 2u, record,
        SLOT_VOLUME_SIZE, 2u) == STORAGE_OK);
    run_until_terminal(&service, &wire, &bus, &now_us);
    assert(service.context.state == STORAGE_STATE_FAILED);
    assert(wire.memory[SLOT_VOLUME_A_ADDR + SLOT_VOLUME_SIZE - 1u] ==
           PERSIST_COMMIT_VALID);
    assert(StorageService_TakeCompletion(&service, &completion));
    assert(completion.status == STORAGE_COMMIT_IO_ERROR);

    wire.fail_transaction = 0u;
    assert(StorageService_StartRestoreVolume(&service) == STORAGE_OK);
    run_until_terminal(&service, &wire, &bus, &now_us);
    StorageRestoreStatus restore_status;
    StorageRestoredVolume restored;
    assert(StorageService_TakeRestoredVolume(
        &service, &restore_status, &restored));
    assert(restore_status == STORAGE_RESTORE_OK);
    assert(restored.forward_volume_ul == 1000u);
    assert(restored.reverse_volume_ul == 10u);
    assert(restored.state_version == 1u);

    puts("F-RAM Storage Pipeline Tests: PASS");
    return 0;
}
