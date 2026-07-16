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

#endif
