#include "services/measurement/measurement_manager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const FlowProfile profile = {
    .id = { .profile_id = 3u, .schema_version = 1u,
            .calibration_version = 4u, .qualification_status = 1u },
    .pipe_area = 1000,
    .path_length = 100000,
    .acoustic_velocity = 1480000
};
static const CalibrationRecord calibration = { 4u, 1024, 0, 10u };
static const VolumeConfig volume_config = {
    .config_version = 2u,
    .maximum_integration_gap_us = UINT64_C(5000000),
    .max_uncheckpointed_volume_ul = UINT64_C(100000),
    .max_interval_s = 3600u,
    .min_spacing_s = 60u
};

static void seed_temperature(DataRepository *repository)
{
    TemperatureResult temperature;
    memset(&temperature, 0, sizeof(temperature));
    temperature.temperature_mdeg_c = 25000;
    temperature.meta.source_generation = 1u;
    temperature.meta.sample_sequence = 1u;
    temperature.meta.result_version = 1u;
    temperature.meta.sample_monotonic_us = 1000u;
    temperature.meta.completion_monotonic_us = 1100u;
    temperature.meta.validity = DATA_VALID;
    temperature.meta.freshness = DATA_FRESH;
    temperature.meta.acceptance = DATA_ACCEPTED;
    temperature.meta.purpose = MEAS_PURPOSE_PRODUCTION;
    temperature.meta.origin = DATA_ORIGIN_LIVE_DEVICE;
    temperature.meta.provenance = PROVENANCE_MEASURED;

    RepoWriteTxn txn;
    txn_init(&txn);
    assert(txn_begin(&txn, repository));
    assert(txn_write_temperature(&txn, &temperature));
    assert(txn_commit(&txn));
}

static void run_sample(MeasurementManager *manager, AppEventQueue *queue,
                       uint64_t sample_us, uint32_t correlation_id)
{
    AppEvent event;
    memset(&event, 0, sizeof(event));
    event.id = EVT_MAX_IRQ_ASSERTED;
    event.monotonic_timestamp_us = sample_us;
    assert(measurement_manager_process_event(manager, &event));

    static const uint8_t frame[MAX35103_FLOW_FRAME_SIZE] = {
        0x01, 0x90, 0x00, 0x00, /* upstream: 100,000,000 ps */
        0x01, 0x90, 0x01, 0x00, /* downstream: +976 ps */
        0xFF, 0xFF, 0xFF, 0x00, /* AVGUP - AVGDN */
        0x01, 0x08              /* range 1, eight valid cycles */
    };
    memset(&event, 0, sizeof(event));
    event.id = EVT_MAX_SPI_COMPLETED;
    event.correlation_id = correlation_id;
    event.monotonic_timestamp_us = sample_us + 100u;
    event.payload_size = sizeof(frame);
    memcpy(event.payload, frame, sizeof(frame));
    assert(measurement_manager_process_event(manager, &event));

    assert(app_event_queue_try_get(queue, &event));
    assert(event.id == EVT_MAX_RAW_READY);
    assert(measurement_manager_process_event(manager, &event));

    const EventId expected[] = {
        EVT_FLOW_RESULT_READY, EVT_VOLUME_UPDATED, EVT_LEAK_RESULT_UPDATED
    };
    for (uint8_t i = 0; i < 3u; ++i) {
        assert(app_event_queue_try_get(queue, &event));
        assert(event.id == expected[i]);
    }
}

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
    seed_temperature(&repository);

    MeasurementManager manager;
    measurement_manager_init(&manager, &queue, &repository);
    LeakDetectionConfig leak_config;
    LeakConfig_GetTestDefaults(&leak_config);
    assert(measurement_manager_bind_flow_pipeline(
        &manager, &profile, &calibration, &volume_config, &leak_config, 0u));

    uint64_t before = repository.snapshot_version;
    run_sample(&manager, &queue, UINT64_C(100000), 10u);
    RuntimeSnapshot first;
    assert(data_repository_snapshot_copy(&repository, &first));
    assert(first.snapshot_version == before + 1u);
    assert(first.flow.flow_ul_per_s > 0);
    assert(first.flow.meta.sample_sequence == 1u);
    assert(first.volume.last_consumed_flow_sequence == 1u);

    before = repository.snapshot_version;
    run_sample(&manager, &queue, UINT64_C(1100000), 11u);
    RuntimeSnapshot second;
    assert(data_repository_snapshot_copy(&repository, &second));
    assert(second.snapshot_version == before + 1u);
    assert(second.flow.meta.sample_sequence == 2u);
    assert(second.volume.last_consumed_flow_sequence == 2u);
    assert(second.volume.forward_volume_ul > 0u);
    assert(second.leak.source_snapshot_version == before);

    puts("Flow/Volume/Leak Atomic Pipeline Tests: PASS");
    return 0;
}
