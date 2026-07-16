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
 * computes volumetric flow and writes FlowResult into the caller transaction.
 */

typedef enum {
    FLOW_OK,                  /* Result was computed and written successfully. */
    FLOW_INVALID_TOF,         /* Either TOF input is outside the profile range. */
    FLOW_TEMPERATURE_MISSING, /* The input snapshot has no usable temperature. */
    FLOW_TEMPERATURE_STALE,   /* Temperature is too old for the TOF sample. */
    FLOW_PROFILE_ERROR,       /* Profile or calibration is unavailable/invalid. */
    FLOW_NUMERIC_ERROR,       /* Checked intermediate arithmetic failed. */
    FLOW_INTERNAL_ERROR       /* Service invariant or transaction write failed. */
} FlowProcessStatus;

typedef struct {
    uint32_t source_id;
    int64_t  flow_ul_per_s;           /* Signed: forward = positive */
    FlowDirection direction;
    uint32_t processing_flags;
    ResultMetadata meta;
} FlowCandidate;

typedef struct {
    AppEventQueue *event_queue; /* Borrowed; owned by AppComposition. */
    DataRepository *repo;       /* Borrowed; owned by AppComposition. */

    /* Profile changes increment this value and invalidate pending samples. */
    uint32_t generation;

    const FlowProfile *active_profile;       /* Borrowed; NULL until configured. */
    const CalibrationRecord *active_cal;     /* Borrowed; NULL until configured. */
    uint32_t accepted_count;                 /* Monotonic diagnostic counter. */
    uint32_t rejected_stale_count;           /* Reset when the profile changes. */
} FlowService;

void flow_service_init(FlowService *svc, AppEventQueue *eq, DataRepository *repo);
void flow_service_set_profile(FlowService *svc, const FlowProfile *p);
void flow_service_set_calibration(FlowService *svc, const CalibrationRecord *c);

// Pure conversion: does not read the repository, mutate service state, or post
// events. profile, cal, and candidate must remain valid for the call.
FlowProcessStatus flow_compute(
    int64_t tof_up_ps, int64_t tof_down_ps,
    int32_t paired_temp_mdeg_c,
    const FlowProfile *profile,
    const CalibrationRecord *cal,
    FlowCandidate *candidate);

// Reads the paired temperature from the transaction input snapshot, computes
// flow, and writes the result to txn. The caller owns txn begin/commit/abort.
// active_profile and active_cal must be configured before this call.
FlowProcessStatus flow_service_accept_tof(
    FlowService *svc,
    int64_t tof_up_ps, int64_t tof_down_ps,
    RepoWriteTxn *txn,
    uint32_t correlation_id);

FlowProcessStatus flow_service_accept_sample(
    FlowService *svc,
    int64_t tof_up_ps,
    int64_t tof_down_ps,
    const ResultMetadata *metadata,
    const TemperatureResult *paired_temperature,
    RepoWriteTxn *txn);

#endif
