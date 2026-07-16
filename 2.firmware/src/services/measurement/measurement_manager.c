#include "services/measurement/measurement_manager.h"
#include <string.h>

#define PRESSURE_I2C_CLIENT_ID 1u
#define PRESSURE_I2C_PRIORITY 1u
#define PRESSURE_I2C_TIMEOUT_US UINT64_C(50000)

static bool pressure_port_submit(void *instance, uint8_t address,
                                 const uint8_t *tx, uint16_t tx_len,
                                 uint8_t *rx, uint16_t rx_len,
                                 uint32_t correlation_id,
                                 uint64_t deadline_us)
{
    MeasurementManager *mgr = instance;
    const I2cPendingTransaction *active = i2c_bus_active(&mgr->i2c_bus);
    if (!active || !i2c_port_is_valid(&mgr->pressure_i2c_port))
        return false;
    I2cPortRequest request = {
        .transaction_id = active->transaction_id,
        .correlation_id = correlation_id,
        .client_generation = active->client_generation,
        .bus_generation = mgr->i2c_bus.bus_generation,
        .slave_address = address,
        .tx = tx,
        .tx_length = tx_len,
        .rx = rx,
        .rx_length = rx_len,
        .deadline_us = deadline_us
    };
    return mgr->pressure_i2c_port.submit(
        mgr->pressure_i2c_port.context, &request) == PORT_OK;
}

static void pressure_bus_complete(
    void *instance, const I2cTransactionCompletion *completion)
{
    MeasurementManager *mgr = instance;
    bool success = completion->result == I2C_TRANSACTION_OK;
    const uint8_t *rx = NULL;
    uint16_t rx_length = 0u;
    if (mgr->zssc.state == ZSSC_STATE_READING ||
        mgr->zssc.state == ZSSC_STATE_POLLING) {
        rx = mgr->zssc.response_buffer;
        rx_length = ZSSC3241_FULL_RESPONSE_SIZE;
    }
    zssc3241_on_i2c_completion(&mgr->zssc, completion->correlation_id,
                                success, rx, rx_length,
                                mgr->pressure_completion_now_us);
}

static bool submit_pressure_start(MeasurementManager *mgr, uint64_t now_us)
{
    const uint8_t *tx = NULL;
    uint16_t tx_length = 0u;
    uint32_t transaction_id = mgr->next_i2c_transaction_id++;
    uint32_t correlation_id = mgr->next_pressure_correlation_id++;
    if (!zssc3241_prepare_start(&mgr->zssc, correlation_id, transaction_id,
                                &tx, &tx_length))
        return false;
    return i2c_bus_submit(&mgr->i2c_bus, PRESSURE_I2C_CLIENT_ID,
                          transaction_id, correlation_id,
                          tx, tx_length, NULL, 0u,
                          now_us + PRESSURE_I2C_TIMEOUT_US,
                          PRESSURE_I2C_PRIORITY);
}

static bool submit_pressure_read(MeasurementManager *mgr, uint64_t now_us)
{
    uint8_t *rx = NULL;
    uint16_t rx_length = 0u;
    if (!zssc3241_prepare_read(&mgr->zssc, &rx, &rx_length))
        return false;
    uint32_t transaction_id = mgr->next_i2c_transaction_id++;
    mgr->zssc.active_transaction_id = transaction_id;
    return i2c_bus_submit(&mgr->i2c_bus, PRESSURE_I2C_CLIENT_ID,
                          transaction_id, mgr->zssc.active_correlation_id,
                          NULL, 0u, rx, rx_length,
                          now_us + PRESSURE_I2C_TIMEOUT_US,
                          PRESSURE_I2C_PRIORITY);
}

static MeasurementServiceResult max_on_event(void *instance,
                                             const AppEvent *event)
{
    MeasurementManager *mgr = (MeasurementManager *)instance;
    switch (event->id) {
    case EVT_MAX_IRQ_ASSERTED:
        max35103_on_irq(&mgr->max, event->monotonic_timestamp_us);
        mgr->max_cycles++;
        return MEASUREMENT_SERVICE_HANDLED;
    case EVT_MAX_SPI_COMPLETED:
        max35103_on_spi_completion(&mgr->max, event->correlation_id, true,
                                   event->payload, event->payload_size,
                                   event->monotonic_timestamp_us);
        return MEASUREMENT_SERVICE_HANDLED;
    case EVT_MAX_SPI_FAILED:
        max35103_on_spi_completion(&mgr->max, event->correlation_id, false,
                                   NULL, 0, event->monotonic_timestamp_us);
        return MEASUREMENT_SERVICE_HANDLED;
    case EVT_MAX_RESULT_TIMEOUT:
        max35103_on_timeout(&mgr->max, event->monotonic_timestamp_us);
        return MEASUREMENT_SERVICE_HANDLED;
    case EVT_MAX_RAW_READY:
        return MEASUREMENT_SERVICE_HANDLED;
    default:
        return MEASUREMENT_SERVICE_IGNORED;
    }
}

static MeasurementServiceResult zssc_on_event(void *instance,
                                              const AppEvent *event)
{
    MeasurementManager *mgr = (MeasurementManager *)instance;
    switch (event->id) {
    case EVT_PRESSURE_SAMPLE_DUE:
        zssc3241_on_sample_due(&mgr->zssc, event->monotonic_timestamp_us);
        mgr->zssc_cycles++;
        if (mgr->pressure_pipeline_bound &&
            !submit_pressure_start(mgr, event->monotonic_timestamp_us))
            return MEASUREMENT_SERVICE_ERROR;
        return MEASUREMENT_SERVICE_HANDLED;
    case EVT_PRESSURE_EOC_ASSERTED:
        zssc3241_on_eoc_asserted(&mgr->zssc, event->monotonic_timestamp_us);
        if (mgr->pressure_pipeline_bound &&
            !submit_pressure_read(mgr, event->monotonic_timestamp_us))
            return MEASUREMENT_SERVICE_ERROR;
        return MEASUREMENT_SERVICE_HANDLED;
    case EVT_PRESSURE_POLL_DUE:
        zssc3241_on_poll_due(&mgr->zssc, event->monotonic_timestamp_us);
        if (mgr->pressure_pipeline_bound &&
            !submit_pressure_read(mgr, event->monotonic_timestamp_us))
            return MEASUREMENT_SERVICE_ERROR;
        return MEASUREMENT_SERVICE_HANDLED;
    case EVT_PRESSURE_TIMEOUT:
        zssc3241_on_timeout(&mgr->zssc, event->monotonic_timestamp_us);
        return MEASUREMENT_SERVICE_HANDLED;
    case EVT_PRESSURE_RAW_READY:
        return MEASUREMENT_SERVICE_HANDLED;
    default:
        return MEASUREMENT_SERVICE_IGNORED;
    }
}

bool measurement_manager_register(MeasurementManager *mgr,
                                  const MeasurementService *service)
{
    if (!mgr || !service || service->service_id == 0
        || (!service->on_event && !service->compute)
        || mgr->service_count >= MEASUREMENT_MANAGER_MAX_SERVICES)
        return false;

    for (uint8_t i = 0; i < mgr->service_count; ++i) {
        if (mgr->services[i].service_id == service->service_id)
            return false;
    }
    mgr->services[mgr->service_count++] = *service;
    return true;
}

bool measurement_manager_set_enabled(MeasurementManager *mgr,
                                     uint32_t service_id,
                                     bool enabled)
{
    if (!mgr) return false;
    for (uint8_t i = 0; i < mgr->service_count; ++i) {
        if (mgr->services[i].service_id == service_id) {
            mgr->services[i].enabled = enabled;
            return true;
        }
    }
    return false;
}

void measurement_manager_init(MeasurementManager *mgr,
                              AppEventQueue *event_queue,
                              DataRepository *repo)
{
    if (!mgr) return;
    memset(mgr, 0, sizeof(*mgr));
    if (!event_queue || !repo) return;
    mgr->event_queue = event_queue;
    mgr->repo = repo;
    mgr->production_enabled = true;
    max35103_init(&mgr->max, event_queue);
    zssc3241_init(&mgr->zssc, event_queue);
    pressure_service_init(&mgr->pressure_service, event_queue);
    flow_service_init(&mgr->flow_service, event_queue, repo);
    VolumeAccumulator_Init(&mgr->volume_accumulator, NULL);
    LeakDetectionConfig leak_defaults;
    LeakConfig_GetTestDefaults(&leak_defaults);
    LeakDetection_Init(&mgr->leak_detection, &leak_defaults, 0u);
    i2c_bus_init(&mgr->i2c_bus);
    mgr->next_i2c_transaction_id = 1u;
    mgr->next_pressure_correlation_id = 1u;

    MeasurementService max_service = {
        .service_id = MEASUREMENT_SERVICE_ID_MAX35103,
        .instance = mgr,
        .on_event = max_on_event,
        .enabled = true
    };
    MeasurementService zssc_service = {
        .service_id = MEASUREMENT_SERVICE_ID_ZSSC3241,
        .instance = mgr,
        .on_event = zssc_on_event,
        .enabled = true
    };
    (void)measurement_manager_register(mgr, &max_service);
    (void)measurement_manager_register(mgr, &zssc_service);
}

bool measurement_manager_process_event(MeasurementManager *mgr,
                                       const AppEvent *event)
{
    if (!mgr || !mgr->repo || !event) return false;

    RuntimeSnapshot input;
    if (!data_repository_snapshot_copy(mgr->repo, &input)) return false;

    RepoWriteTxn txn;
    txn_init(&txn);
    if (!txn_begin(&txn, mgr->repo)) return false;

    MeasurementComputeContext context = {
        .input = &input,
        .output = &txn,
        .event = event,
        .now_us = event->monotonic_timestamp_us
    };
    bool consumed = false;
    bool failed = false;
    bool flow_state_prepared = false;
    VolumeAccumulator next_volume;
    LeakDetectionService next_leak;

    for (uint8_t i = 0; i < mgr->service_count; ++i) {
        MeasurementService *service = &mgr->services[i];
        if (!service->enabled) continue;

        MeasurementServiceResult result = MEASUREMENT_SERVICE_IGNORED;
        if (service->on_event)
            result = service->on_event(service->instance, event);
        if (result == MEASUREMENT_SERVICE_ERROR) {
            failed = true;
            break;
        }
        consumed = consumed || result != MEASUREMENT_SERVICE_IGNORED;

        if (service->compute) {
            result = service->compute(service->instance, &context);
            if (result == MEASUREMENT_SERVICE_ERROR) {
                failed = true;
                break;
            }
            consumed = consumed || result != MEASUREMENT_SERVICE_IGNORED;
        }
    }

    if (!failed && event->id == EVT_PRESSURE_RAW_READY) {
        ZsscRawPressureSample sample;
        if (!zssc3241_take_raw_pressure(&mgr->zssc, &sample)) {
            failed = true;
        } else {
            PressureProcessStatus pressure_status = pressure_service_accept_sample(
                &mgr->pressure_service, sample.raw_u24, sample.status,
                &sample.meta, &txn);
            if (pressure_status != PRESSURE_OK)
                failed = true;
        }
    }

    if (!failed && event->id == EVT_MAX_RAW_READY) {
        Max35103RawFlowSample raw;
        if (!mgr->flow_pipeline_bound ||
            !max35103_take_raw_flow(&mgr->max, &raw)) {
            failed = true;
        } else {
            FlowProcessStatus flow_status = flow_service_accept_sample(
                &mgr->flow_service, raw.tof_up_ps, raw.tof_down_ps,
                &raw.meta, &input.temperature, &txn);
            if (flow_status != FLOW_OK) {
                failed = true;
            } else {
                RuntimeSnapshot candidate_snapshot;
                if (!txn_read_snapshot(&txn, &candidate_snapshot)) {
                    failed = true;
                } else {
                    /* Candidate-copy pattern: stateful services are applied to
                     * the live owners only after repository commit succeeds. */
                    next_volume = mgr->volume_accumulator;
                    VolumeConsumeStatus volume_status = VolumeAccumulator_Consume(
                        &next_volume, &candidate_snapshot.flow);
                    if (volume_status != VOLUME_OK &&
                        volume_status != VOLUME_ANCHORED &&
                        volume_status != VOLUME_ZERO_INTERVAL) {
                        failed = true;
                    } else if (!txn_write_volume(
                                   &txn, VolumeAccumulator_GetState(&next_volume))) {
                        failed = true;
                    } else {
                        next_leak = mgr->leak_detection;
                        LeakInputView leak_input;
                        memset(&leak_input, 0, sizeof(leak_input));
                        leak_input.evaluation_monotonic_us =
                            raw.meta.completion_monotonic_us;
                        leak_input.sample_sequence = raw.meta.sample_sequence;
                        leak_input.source_generation = raw.meta.source_generation;
                        leak_input.flow_usable =
                            candidate_snapshot.flow.meta.validity == DATA_VALID &&
                            candidate_snapshot.flow.meta.freshness == DATA_FRESH &&
                            candidate_snapshot.flow.meta.acceptance == DATA_ACCEPTED;
                        leak_input.flow_ul_per_s = candidate_snapshot.flow.flow_ul_per_s;
                        leak_input.flow_direction = candidate_snapshot.flow.direction;
                        leak_input.flow_result = &candidate_snapshot.flow;
                        leak_input.pressure_usable =
                            candidate_snapshot.pressure.meta.validity == DATA_VALID &&
                            candidate_snapshot.pressure.meta.freshness == DATA_FRESH &&
                            candidate_snapshot.pressure.meta.acceptance == DATA_ACCEPTED;
                        leak_input.pressure_pa = candidate_snapshot.pressure.pressure_pa;
                        leak_input.pressure_result = &candidate_snapshot.pressure;
                        LeakUpdateStatus leak_status = LeakDetection_Evaluate(
                            &next_leak, &leak_input);
                        if (leak_status == LEAK_UPDATE_INTERNAL_ERROR ||
                            leak_status == LEAK_UPDATE_CONFIG_ERROR) {
                            failed = true;
                        } else {
                            LeakDetectionResult leak_result;
                            LeakDetection_GetResult(
                                &next_leak, &leak_input,
                                VolumeAccumulator_GetState(&next_volume),
                                input.snapshot_version, &leak_result);
                            if (!txn_write_leak(&txn, &leak_result))
                                failed = true;
                            else
                                flow_state_prepared = true;
                        }
                    }
                }
            }
        }
    }

    if (failed) {
        txn_abort(&txn);
        return false;
    }
    if (txn.written_fields_mask != 0) {
        if (!txn_commit(&txn)) return false;
        if (flow_state_prepared) {
            mgr->volume_accumulator = next_volume;
            mgr->leak_detection = next_leak;
            const EventId ids[] = {
                EVT_FLOW_RESULT_READY,
                EVT_VOLUME_UPDATED,
                EVT_LEAK_RESULT_UPDATED
            };
            for (uint8_t i = 0; i < 3u; ++i) {
                AppEvent ready;
                memset(&ready, 0, sizeof(ready));
                ready.id = ids[i];
                ready.priority = EVENT_PRIO_MEASUREMENT;
                ready.delivery = DELIVERY_EDGE;
                ready.correlation_id = event->correlation_id;
                ready.monotonic_timestamp_us = event->monotonic_timestamp_us;
                (void)app_event_queue_post(mgr->event_queue, &ready);
            }
        }
        if (event->id == EVT_PRESSURE_RAW_READY) {
            AppEvent ready;
            memset(&ready, 0, sizeof(ready));
            ready.id = EVT_PRESSURE_RESULT_READY;
            ready.priority = EVENT_PRIO_MEASUREMENT;
            ready.delivery = DELIVERY_EDGE;
            ready.correlation_id = event->correlation_id;
            ready.monotonic_timestamp_us = event->monotonic_timestamp_us;
            (void)app_event_queue_post(mgr->event_queue, &ready);
        }
    } else {
        txn_abort(&txn);
    }
    return consumed;
}

bool measurement_manager_bind_flow_pipeline(
    MeasurementManager *mgr,
    const FlowProfile *profile,
    const CalibrationRecord *calibration,
    const VolumeConfig *volume_config,
    const LeakDetectionConfig *leak_config,
    uint64_t now_us)
{
    if (!mgr || !profile || !calibration || !volume_config || !leak_config ||
        !LeakConfig_Validate(leak_config, NULL, 0u))
        return false;
    flow_service_set_profile(&mgr->flow_service, profile);
    flow_service_set_calibration(&mgr->flow_service, calibration);
    VolumeAccumulator_Init(&mgr->volume_accumulator, volume_config);
    LeakDetection_Init(&mgr->leak_detection, leak_config, now_us);
    mgr->flow_pipeline_bound = true;
    return true;
}

bool measurement_manager_bind_pressure_pipeline(
    MeasurementManager *mgr,
    const I2cPort *i2c_port,
    uint8_t slave_address,
    const PressureProfile *profile,
    const CalibrationRecord *calibration)
{
    if (!mgr || !i2c_port_is_valid(i2c_port) || !profile || !calibration)
        return false;
    mgr->pressure_i2c_port = *i2c_port;
    mgr->pressure_i2c_address = slave_address;
    pressure_service_set_profile(&mgr->pressure_service, profile);
    pressure_service_set_calibration(&mgr->pressure_service, calibration);
    I2cBusClient client = {
        .client_id = PRESSURE_I2C_CLIENT_ID,
        .slave_address = slave_address,
        .client_generation = mgr->zssc.generation,
        .context = mgr,
        .submit_tx = pressure_port_submit,
        .on_complete = pressure_bus_complete
    };
    if (!i2c_bus_register_client(&mgr->i2c_bus, &client))
        return false;
    mgr->pressure_pipeline_bound = true;
    return true;
}

bool measurement_manager_complete_i2c(
    MeasurementManager *mgr,
    uint32_t transaction_id,
    uint32_t correlation_id,
    uint32_t client_generation,
    uint32_t bus_generation,
    I2cTransactionResult result,
    uint64_t completion_now_us)
{
    if (!mgr) return false;
    mgr->pressure_completion_now_us = completion_now_us;
    return i2c_bus_complete(&mgr->i2c_bus, transaction_id, correlation_id,
                            client_generation, bus_generation, result);
}
