#ifndef SWFPM_LEAK_DETECTION_H
#define SWFPM_LEAK_DETECTION_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/product/leak_types.h"
#include "domain/measurement/measurement_types.h"
#include "domain/product/volume_types.h"
#include "domain/common/metadata.h"
#include "services/leak/leak_config.h"
#include "services/leak/leak_tracker.h"

#define LEAK_MAX_TRACKERS 4

typedef struct {
    uint64_t          evaluation_monotonic_us;
    uint64_t          sample_sequence;
    uint32_t          source_generation;
    bool              flow_usable;
    int64_t           flow_ul_per_s;
    FlowDirection     flow_direction;
    bool              pressure_usable;
    int32_t           pressure_pa;
    const FlowResult *flow_result;
    const PressureResult *pressure_result;
} LeakInputView;

typedef enum {
    LEAK_UPDATE_NO_CHANGE,
    LEAK_UPDATE_RESULT_CHANGED,
    LEAK_UPDATE_STATE_CHANGED,
    LEAK_UPDATE_REJECTED_DUPLICATE,
    LEAK_UPDATE_REJECTED_STALE,
    LEAK_UPDATE_CONFIG_ERROR,
    LEAK_UPDATE_INTERNAL_ERROR
} LeakUpdateStatus;

typedef struct {
    LeakDetectionConfig cfg;

    LeakState            state;
    LeakEvaluationStatus eval_status;
    LeakPrimaryReason    primary_reason;
    LeakSeverity         severity;
    uint32_t             reason_flags;
    uint32_t             evidence_flags;
    uint32_t             quality_flags;

    uint64_t             state_version;
    uint64_t             state_change_sequence;
    uint64_t             state_entered_us;

    LeakEvidenceTracker  continuous_tracker;
    LeakEvidenceTracker  burst_tracker;
    LeakEvidenceTracker  low_pressure_tracker;
    LeakEvidenceTracker  high_pressure_tracker;

    uint64_t             last_flow_sequence;
    uint64_t             last_pressure_sequence;
    uint64_t             last_eval_us;
    uint32_t             runtime_generation;

    uint64_t             diag_evals;
    uint64_t             diag_updates;
    uint64_t             diag_duplicates;
    uint64_t             diag_stale;
    uint64_t             diag_flow_unusable;
    uint64_t             diag_pressure_unusable;
} LeakDetectionService;

void LeakDetection_Init(LeakDetectionService *svc, const LeakDetectionConfig *cfg, uint64_t now_us);
LeakUpdateStatus LeakDetection_Evaluate(LeakDetectionService *svc, const LeakInputView *input);
bool LeakDetection_ApplyConfig(LeakDetectionService *svc, const LeakDetectionConfig *cfg, uint64_t now_us);
void LeakDetection_Reset(LeakDetectionService *svc, uint32_t new_generation, uint64_t now_us);

#endif
