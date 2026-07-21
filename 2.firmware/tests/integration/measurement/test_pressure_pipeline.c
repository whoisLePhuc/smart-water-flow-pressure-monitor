#include "services/measurement/measurement_manager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    I2cPortRequest request;
    bool active;
    uint32_t submissions;
} FakeI2cPort;

static PortStatus submit(void *context, const I2cPortRequest *request)
{
    FakeI2cPort *fake = context;
    if (fake->active) return PORT_STATUS_BUSY;
    fake->request = *request;
    fake->active = true;
    fake->submissions++;
    return PORT_OK;
}

static void complete(MeasurementManager *manager, FakeI2cPort *fake,
                     uint64_t now_us)
{
    I2cPortRequest request = fake->request;
    fake->active = false;
    assert(measurement_manager_complete_i2c(
        manager, request.transaction_id, request.correlation_id,
        request.client_generation, request.bus_generation,
        I2C_TRANSACTION_OK, now_us));
}

static const PressureProfile profile = {
    .id = { 7u, 1u, 2u, 1u },
    .pa_min = 0,
    .pa_max = 1000000,
    .endpoint_lo_raw = 0,
    .endpoint_hi_raw = 0xFFFFFF,
    .endpoint_lo_pa = 0,
    .endpoint_hi_pa = 1000000
};
static const CalibrationRecord calibration = { 2u, 1024, 0, 10u };

int main(void)
{
    AppEventQueue queue;
    AppEventQueueConfig qconfig = {
        APP_EVENT_QUEUE_DEFAULT_CAPACITY,
        APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL,
        APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT
    };
    app_event_queue_init(&queue, &qconfig);
    DataRepository repository;
    data_repository_init(&repository);
    MeasurementManager manager;
    measurement_manager_init(&manager, &queue, &repository);

    FakeI2cPort fake;
    memset(&fake, 0, sizeof(fake));
    I2cPort port = { .context = &fake, .submit = submit };
    I2cBusManager bus;
    i2c_bus_init(&bus, &port);
    assert(measurement_manager_bind_pressure_pipeline(
        &manager, &bus, 0x28u, &profile, &calibration));

    AppEvent event;
    memset(&event, 0, sizeof(event));
    event.id = EVT_PRESSURE_SAMPLE_DUE;
    event.monotonic_timestamp_us = 1000u;
    assert(measurement_manager_process_event(&manager, &event));
    assert(fake.active && fake.request.tx_length == 1u);
    assert(fake.request.tx[0] == ZSSC3241_CMD_FULL_MEASURE);
    complete(&manager, &fake, 1050u);

    event.id = EVT_PRESSURE_EOC_ASSERTED;
    event.monotonic_timestamp_us = 5000u;
    assert(measurement_manager_process_event(&manager, &event));
    assert(fake.active && fake.request.rx_length == ZSSC3241_FULL_RESPONSE_SIZE);
    fake.request.rx[0] = 0x40u;
    fake.request.rx[1] = 0x80u;
    fake.request.rx[2] = 0x00u;
    fake.request.rx[3] = 0x00u;
    complete(&manager, &fake, 5050u);

    assert(app_event_queue_try_get(&queue, &event));
    assert(event.id == EVT_PRESSURE_RAW_READY);
    assert(measurement_manager_process_event(&manager, &event));

    RuntimeSnapshot snapshot;
    assert(data_repository_snapshot_copy(&repository, &snapshot));
    assert(snapshot.pressure.pressure_pa > 490000);
    assert(snapshot.pressure.pressure_pa < 510000);
    assert(snapshot.pressure.meta.sample_sequence == 1u);
    assert(snapshot.pressure.meta.validity == DATA_VALID);

    assert(app_event_queue_try_get(&queue, &event));
    assert(event.id == EVT_PRESSURE_RESULT_READY);
    puts("Pressure Pipeline Integration Tests: PASS");
    return 0;
}
