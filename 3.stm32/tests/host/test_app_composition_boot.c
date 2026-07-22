#include "app/app_composition.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t memory[FM24CL04B_SIZE_BYTES];
    I2cPortRequest request;
    AppComposition* app;
    uint32_t transaction_count;
    uint32_t fail_transaction;
    bool active;
} FakeFramWire;

static PortStatus wire_submit(void* context, const I2cPortRequest* request) {
    FakeFramWire* wire = context;
    if (!wire || !request)
        return PORT_STATUS_INVALID_PARAM;
    if (wire->active)
        return PORT_STATUS_BUSY;
    wire->request = *request;
    wire->transaction_count++;
    wire->active = true;
    return PORT_OK;
}

static PortStatus wire_cancel(void* context,
                              uint32_t transaction_id,
                              uint32_t bus_generation) {
    FakeFramWire* wire = context;
    if (!wire || !wire->active || wire->request.transaction_id != transaction_id
        || wire->request.bus_generation != bus_generation) {
        return PORT_STATUS_INVALID_PARAM;
    }
    wire->active = false;
    return PORT_OK;
}

static PortStatus wire_recover(void* context, uint32_t new_bus_generation) {
    FakeFramWire* wire = context;
    (void)new_bus_generation;
    if (!wire)
        return PORT_STATUS_INVALID_PARAM;
    wire->active = false;
    return PORT_OK;
}

static void wire_complete(FakeFramWire* wire) {
    assert(wire && wire->active && wire->app);
    I2cPortRequest request = wire->request;
    PortStatus result = wire->fail_transaction == wire->transaction_count
                            ? PORT_STATUS_HARDWARE_ERROR
                            : PORT_OK;

    if (result == PORT_OK) {
        assert(request.tx && request.tx_length >= 1u);
        uint16_t address =
            (uint16_t)(((uint16_t)(request.slave_address & 0x01u) << 8u)
                       | request.tx[0]);
        uint16_t write_length = request.tx_length > 1u
                                    ? (uint16_t)(request.tx_length - 1u)
                                    : 0u;
        assert((uint32_t)address + write_length <= sizeof(wire->memory));
        assert((uint32_t)address + request.rx_length <= sizeof(wire->memory));
        if (write_length > 0u)
            memcpy(wire->memory + address, request.tx + 1u, write_length);
        if (request.rx_length > 0u)
            memcpy(request.rx, wire->memory + address, request.rx_length);
    }

    wire->active = false;
    assert(app_composition_on_i2c_port_completion(wire->app, &request, result));
}

static void initialize_app(AppComposition* app, FakeFramWire* wire) {
    memset(app, 0, sizeof(*app));
    wire->app = app;
    I2cPort port = {.context = wire,
                    .submit = wire_submit,
                    .cancel = wire_cancel,
                    .recover = wire_recover};
    AppCompositionDependencies dependencies = {
        .shared_i2c_port = &port,
        .fram_config = {.client_id = 1u,
                        .slave_address_base_7bit = 0x50u,
                        .capacity_bytes = FM24CL04B_SIZE_BYTES,
                        .max_chunk_bytes = FM24CL04B_MAX_CHUNK_BYTES,
                        .bus_priority = 3u},
        .storage_io_timeout_us = 50000u};

    assert(app_composition_init(app, &dependencies));
    assert(app->initialized);
    assert(app->startup_state == APP_STARTUP_RESTORE_START);
    assert(app->restored_volume.selected_slot == SLOT_INDEX_NONE);
    assert(!app->storage_ready);
    assert(!app->runtime_ready);
}

static void run_restore(AppComposition* app, FakeFramWire* wire) {
    uint64_t now_us = 1000u;
    assert(app_composition_start(app));
    assert(app->startup_state == APP_STARTUP_RESTORE_WAIT);
    assert(app->restore_attempts == 1u);
    assert(!app_composition_start(app));

    for (unsigned int i = 0u; i < 256u; ++i) {
        app_composition_poll(app, now_us);
        if (wire->active)
            wire_complete(wire);
        if (app->startup_state == APP_STARTUP_STORAGE_READY
            || app->startup_state == APP_STARTUP_RECOVERY_REQUIRED) {
            return;
        }
        now_us += 100u;
    }
    assert(false && "application boot restore did not terminate");
}

static void assert_result_consumed_once(AppComposition* app) {
    StorageRestoreStatus status;
    StorageRestoredVolume volume;
    AppStartupState terminal_state = app->startup_state;
    assert(!StorageService_TakeRestoredVolume(&app->storage_service,
                                              &status,
                                              &volume));
    app_composition_poll(app, 100000u);
    assert(app->startup_state == terminal_state);
}

static void test_empty_restore(void) {
    AppComposition app;
    FakeFramWire wire;
    memset(&wire, 0, sizeof(wire));
    initialize_app(&app, &wire);
    run_restore(&app, &wire);

    assert(app.startup_state == APP_STARTUP_STORAGE_READY);
    assert(app.restore_status == STORAGE_RESTORE_EMPTY);
    assert(app.storage_ready);
    assert(!app.runtime_ready);
    assert(app.restored_volume.selected_slot == SLOT_INDEX_NONE);
    assert(app.restored_volume.slot_a_reason == SLOT_EMPTY_UNINITIALIZED);
    assert(app.restored_volume.slot_b_reason == SLOT_EMPTY_UNINITIALIZED);
    assert_result_consumed_once(&app);
}

static void test_valid_restore(void) {
    AppComposition app;
    FakeFramWire wire;
    memset(&wire, 0, sizeof(wire));
    assert(StorageRecord_EncodeVolume(wire.memory + SLOT_VOLUME_A_ADDR,
                                      17u,
                                      123456789ull,
                                      9876543ull,
                                      11u,
                                      22u,
                                      33u,
                                      44u,
                                      5u)
           == SLOT_VOLUME_SIZE);
    wire.memory[SLOT_VOLUME_A_ADDR + SLOT_VOLUME_SIZE - 1u] =
        PERSIST_COMMIT_VALID;
    initialize_app(&app, &wire);
    run_restore(&app, &wire);

    assert(app.startup_state == APP_STARTUP_STORAGE_READY);
    assert(app.restore_status == STORAGE_RESTORE_OK);
    assert(app.storage_ready);
    assert(!app.runtime_ready);
    assert(app.restored_volume.forward_volume_ul == 123456789ull);
    assert(app.restored_volume.reverse_volume_ul == 9876543ull);
    assert(app.restored_volume.forward_remainder == 11u);
    assert(app.restored_volume.reverse_remainder == 22u);
    assert(app.restored_volume.state_version == 33u);
    assert(app.restored_volume.last_flow_sequence == 44u);
    assert(app.restored_volume.last_source_generation == 5u);
    assert(app.restored_volume.record_sequence == 17u);
    assert(app.restored_volume.selected_slot == 0u);
    assert_result_consumed_once(&app);
}

static void test_corrupt_restore_requires_recovery(void) {
    AppComposition app;
    FakeFramWire wire;
    memset(&wire, 0, sizeof(wire));
    memset(wire.memory + SLOT_VOLUME_A_ADDR, 0xA5, SLOT_VOLUME_SIZE);
    memset(wire.memory + SLOT_VOLUME_B_ADDR, 0x5A, SLOT_VOLUME_SIZE);
    initialize_app(&app, &wire);
    run_restore(&app, &wire);

    assert(app.startup_state == APP_STARTUP_RECOVERY_REQUIRED);
    assert(app.restore_status == STORAGE_RESTORE_CORRUPT);
    assert(!app.storage_ready);
    assert(!app.runtime_ready);
    assert_result_consumed_once(&app);
}

static void test_io_error_requires_recovery(void) {
    AppComposition app;
    FakeFramWire wire;
    memset(&wire, 0, sizeof(wire));
    wire.fail_transaction = 1u;
    initialize_app(&app, &wire);
    run_restore(&app, &wire);

    assert(app.startup_state == APP_STARTUP_RECOVERY_REQUIRED);
    assert(app.restore_status == STORAGE_RESTORE_IO_ERROR);
    assert(!app.storage_ready);
    assert(!app.runtime_ready);
    assert_result_consumed_once(&app);
}

int main(void) {
    test_empty_restore();
    test_valid_restore();
    test_corrupt_restore_requires_recovery();
    test_io_error_requires_recovery();
    puts("AppComposition Boot Tests: PASS");
    return 0;
}
