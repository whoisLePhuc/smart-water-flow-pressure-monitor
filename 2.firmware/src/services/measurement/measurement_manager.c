#include "services/measurement/measurement_manager.h"
#include <string.h>

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
        return MEASUREMENT_SERVICE_HANDLED;
    case EVT_PRESSURE_EOC_ASSERTED:
        zssc3241_on_eoc_asserted(&mgr->zssc, event->monotonic_timestamp_us);
        return MEASUREMENT_SERVICE_HANDLED;
    case EVT_PRESSURE_POLL_DUE:
        zssc3241_on_poll_due(&mgr->zssc, event->monotonic_timestamp_us);
        return MEASUREMENT_SERVICE_HANDLED;
    case EVT_PRESSURE_TIMEOUT:
        zssc3241_on_timeout(&mgr->zssc, event->monotonic_timestamp_us);
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

    if (failed) {
        txn_abort(&txn);
        return false;
    }
    if (txn.written_fields_mask != 0) {
        if (!txn_commit(&txn)) return false;
    } else {
        txn_abort(&txn);
    }
    return consumed;
}
