#ifndef SWFPM_MEASUREMENT_MANAGER_H
#define SWFPM_MEASUREMENT_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "domain/measurement/measurement_types.h"
#include "infrastructure/queues/app_event_queue.h"
#include "infrastructure/repositories/data_repository.h"
#include "infrastructure/repositories/repo_transaction.h"
#include "max35103.h"
#include "zssc3241.h"
#include "infrastructure/bus/i2c_bus_manager.h"
#include "ports/i2c_port.h"
#include "services/processing/pressure_service.h"
#include "services/processing/flow_service.h"
#include "services/volume/volume_accumulator.h"
#include "services/leak/leak_detection.h"

#define MEASUREMENT_MANAGER_MAX_SERVICES 16u
#define MEASUREMENT_SERVICE_ID_MAX35103  1u
#define MEASUREMENT_SERVICE_ID_ZSSC3241  2u

typedef enum {
    MEASUREMENT_SERVICE_IGNORED = 0,    /* Event was not relevant to this service. */
    MEASUREMENT_SERVICE_HANDLED,        /* State changed; no repository field written. */
    MEASUREMENT_SERVICE_OUTPUT_WRITTEN, /* Service wrote through context->output. */
    MEASUREMENT_SERVICE_ERROR           /* Abort the shared transaction. */
} MeasurementServiceResult;

typedef struct {
    const RuntimeSnapshot *input; /* Borrowed immutable pre-dispatch snapshot. */
    RepoWriteTxn *output;          /* Borrowed active transaction; manager owns lifecycle. */
    const AppEvent *event;         /* Borrowed; valid only for this dispatch. */
    uint64_t               now_us;
} MeasurementComputeContext;

typedef MeasurementServiceResult (*MeasurementEventFn)(
    void *service, const AppEvent *event);
typedef MeasurementServiceResult (*MeasurementComputeFn)(
    void *service, const MeasurementComputeContext *context);

/* A service is a small strategy object. The manager owns only the entry;
 * the concrete service state remains instance-owned by the composition root. */
typedef struct {
    uint32_t             service_id;
    void                *instance; /* Borrowed; concrete owner must outlive manager. */
    MeasurementEventFn   on_event;
    MeasurementComputeFn compute;
    bool                 enabled;
} MeasurementService;

typedef struct {
    Max35103Driver max;
    Zssc3241Driver zssc;
    PressureService pressure_service;
    FlowService flow_service;
    VolumeAccumulator volume_accumulator;
    LeakDetectionService leak_detection;
    I2cBusManager *i2c_bus; /* Borrowed shared bus; owned by composition root. */
    uint8_t pressure_i2c_address;
    uint32_t next_pressure_correlation_id;
    uint64_t pressure_completion_now_us;
    bool pressure_pipeline_bound;
    bool flow_pipeline_bound;

    AppEventQueue  *event_queue; /* Borrowed; owned by AppComposition. */
    DataRepository *repo;        /* Borrowed; owned by AppComposition. */
    MeasurementService services[MEASUREMENT_MANAGER_MAX_SERVICES];
    uint8_t service_count;
    bool production_enabled;

    uint32_t max_cycles;  /* Monotonic diagnostic count for accepted MAX IRQs. */
    uint32_t zssc_cycles; /* Monotonic diagnostic count for pressure due events. */
} MeasurementManager;

void measurement_manager_init(MeasurementManager *mgr,
                              AppEventQueue *event_queue,
                              DataRepository *repo);

bool measurement_manager_register(MeasurementManager *mgr,
                                  const MeasurementService *service);
bool measurement_manager_set_enabled(MeasurementManager *mgr,
                                     uint32_t service_id,
                                     bool enabled);

// Runs enabled services in registration order. All compute callbacks share one
// repository transaction so consumers cannot observe partial service output.
// Any MEASUREMENT_SERVICE_ERROR aborts the entire dispatch transaction.
bool measurement_manager_process_event(MeasurementManager *mgr,
                                       const AppEvent *event);

bool measurement_manager_bind_pressure_pipeline(
    MeasurementManager *mgr,
    I2cBusManager *i2c_bus,
    uint8_t slave_address,
    const PressureProfile *profile,
    const CalibrationRecord *calibration);

/* Called by the platform I2C adapter from cooperative callback context. ISR
 * callbacks should only post/copy a completion event before invoking this. */
bool measurement_manager_complete_i2c(
    MeasurementManager *mgr,
    uint32_t transaction_id,
    uint32_t correlation_id,
    uint32_t client_generation,
    uint32_t bus_generation,
    I2cTransactionResult result,
    uint64_t completion_now_us);

bool measurement_manager_bind_flow_pipeline(
    MeasurementManager *mgr,
    const FlowProfile *profile,
    const CalibrationRecord *calibration,
    const VolumeConfig *volume_config,
    const LeakDetectionConfig *leak_config,
    uint64_t now_us);

#endif
