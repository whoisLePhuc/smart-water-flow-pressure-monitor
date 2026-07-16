#ifndef SWFPM_FLOW_SERVICE_H
#define SWFPM_FLOW_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/measurement/measurement_types.h"
#include "infrastructure/queues/app_event_queue.h"
#include "infrastructure/repositories/data_repository.h"
#include "infrastructure/repositories/repo_transaction.h"
#include "services/configuration/sensor_profile.h"

/* FlowComputationService — TOF-based flow computation.
 * Receives MAX upstream/downstream TOF, pairs with temperature,
 * computes volumetric flow, publishes FlowResult.
 * Volume accumulation NOT in Phase 9.
 */

typedef enum {
    FLOW_OK,
    FLOW_INVALID_TOF,
    FLOW_TEMPERATURE_MISSING,
    FLOW_TEMPERATURE_STALE,
    FLOW_PROFILE_ERROR,
    FLOW_NUMERIC_ERROR,
    FLOW_INTERNAL_ERROR
} FlowProcessStatus;

typedef struct {
    uint32_t source_id;
    int64_t  flow_ul_per_s;           /* Signed: forward = positive */
    FlowDirection direction;
    uint32_t processing_flags;
    ResultMetadata meta;
} FlowCandidate;

typedef struct {
    AppEventQueue               *event_queue;
    DataRepository              *repo;
    uint32_t                     generation;
    const FlowProfile           *active_profile;
    const CalibrationRecord     *active_cal;
    uint32_t accepted_count;
    uint32_t rejected_stale_count;
} FlowService;

void flow_service_init(FlowService *svc, AppEventQueue *eq, DataRepository *repo);
void flow_service_set_profile(FlowService *svc, const FlowProfile *p);
void flow_service_set_calibration(FlowService *svc, const CalibrationRecord *c);

/* Pure TOF → flow conversion */
FlowProcessStatus flow_compute(
    int64_t tof_up_ps, int64_t tof_down_ps,
    int32_t paired_temp_mdeg_c,
    const FlowProfile *profile,
    const CalibrationRecord *cal,
    FlowCandidate *candidate);

/* Stateful accept with temperature lookup from repository */
FlowProcessStatus flow_service_accept_tof(
    FlowService *svc,
    int64_t tof_up_ps, int64_t tof_down_ps,
    RepoWriteTxn *txn,
    uint32_t correlation_id);

#endif
