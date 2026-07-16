#include "services/measurement/measurement_manager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned call_order;
    unsigned *sequence;
    bool write_power;
} FakeMeasurement;

static MeasurementServiceResult fake_compute(
    void *instance, const MeasurementComputeContext *context)
{
    FakeMeasurement *fake = (FakeMeasurement *)instance;
    fake->call_order = ++*fake->sequence;
    if (fake->write_power) {
        PowerSnapshot power;
        memset(&power, 0, sizeof(power));
        power.battery_mv = 3710;
        power.available = true;
        return txn_write_power(context->output, &power)
            ? MEASUREMENT_SERVICE_OUTPUT_WRITTEN
            : MEASUREMENT_SERVICE_ERROR;
    }
    PressureResult pressure;
    memset(&pressure, 0, sizeof(pressure));
    pressure.pressure_pa = 12345;
    return txn_write_pressure(context->output, &pressure)
        ? MEASUREMENT_SERVICE_OUTPUT_WRITTEN
        : MEASUREMENT_SERVICE_ERROR;
}

int main(void)
{
    AppEventQueue queue;
    AppEventQueueConfig config = {
        APP_EVENT_QUEUE_DEFAULT_CAPACITY,
        APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL,
        APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT
    };
    DataRepository repo;
    MeasurementManager manager;
    unsigned sequence = 0;
    FakeMeasurement pressure = {0, &sequence, false};
    FakeMeasurement battery = {0, &sequence, true};

    app_event_queue_init(&queue, &config);
    data_repository_init(&repo);
    measurement_manager_init(&manager, &queue, &repo);

    MeasurementService pressure_service = {
        .service_id = 10,
        .instance = &pressure,
        .compute = fake_compute,
        .enabled = true
    };
    MeasurementService battery_service = {
        .service_id = 11,
        .instance = &battery,
        .compute = fake_compute,
        .enabled = true
    };
    assert(measurement_manager_register(&manager, &pressure_service));
    assert(measurement_manager_register(&manager, &battery_service));
    assert(!measurement_manager_register(&manager, &battery_service));

    AppEvent event;
    memset(&event, 0, sizeof(event));
    event.id = EVT_FLOW_PROCESSING_COMPLETED;
    event.monotonic_timestamp_us = 500;
    assert(measurement_manager_process_event(&manager, &event));

    RuntimeSnapshot snapshot;
    assert(data_repository_snapshot_copy(&repo, &snapshot));
    assert(snapshot.snapshot_version == 1);
    assert(snapshot.pressure.pressure_pa == 12345);
    assert(snapshot.power.battery_mv == 3710);
    assert(pressure.call_order < battery.call_order);

    assert(measurement_manager_set_enabled(&manager, 11, false));
    sequence = 0;
    pressure.call_order = 0;
    battery.call_order = 0;
    assert(measurement_manager_process_event(&manager, &event));
    assert(pressure.call_order == 1);
    assert(battery.call_order == 0);

    puts("Measurement Manager Tests: PASS");
    return 0;
}
