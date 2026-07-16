#ifndef SWFPM_DOMAIN_LEAK_TYPES_H
#define SWFPM_DOMAIN_LEAK_TYPES_H

/* =================================================================
 * Domain: product/leak
 * Owner: domain/product (fw_domain_product)
 *
 * Leak detection result — the outcome of evaluating flow, pressure,
 * and volume data for leak conditions.
 * ================================================================= */

#include <stdint.h>
#include "domain/common/metadata.h"

/* ── Leak state ── */

typedef enum {
    LEAK_STATE_NORMAL,
    LEAK_STATE_SUSPECTED,
    LEAK_STATE_CONFIRMED
} LeakState;

/* ── Leak evaluation status ── */

typedef enum {
    LEAK_EVAL_NOT_READY,
    LEAK_EVAL_ACTIVE,
    LEAK_EVAL_DEGRADED,
    LEAK_EVAL_UNAVAILABLE
} LeakEvaluationStatus;

/* ── Leak evidence flags ── */

typedef enum {
    LEAK_EVIDENCE_CONTINUOUS_FLOW = 1u << 0,
    LEAK_EVIDENCE_HIGH_FLOW       = 1u << 1,
    LEAK_EVIDENCE_LOW_PRESSURE    = 1u << 2,
    LEAK_EVIDENCE_HIGH_PRESSURE   = 1u << 3,
    LEAK_EVIDENCE_PRESSURE_DROP   = 1u << 4,
    LEAK_EVIDENCE_FLOW_PRESSURE_CORRELATED = 1u << 5
} LeakEvidenceFlag;

/* ── Leak primary reason ── */

typedef enum {
    LEAK_REASON_NONE,
    LEAK_REASON_CONTINUOUS_FLOW,
    LEAK_REASON_HIGH_FLOW_BURST
} LeakPrimaryReason;

/* ── Leak severity ── */

typedef enum {
    LEAK_SEVERITY_NONE,
    LEAK_SEVERITY_LOW,
    LEAK_SEVERITY_MEDIUM,
    LEAK_SEVERITY_HIGH
} LeakSeverity;

/* ── Leak detection result ── */

typedef struct {
    uint64_t    result_version;
    uint64_t    state_change_sequence;
    LeakState              state;
    LeakEvaluationStatus   evaluation_status;
    LeakPrimaryReason      primary_reason;
    LeakSeverity           severity;
    uint32_t    reason_flags;
    uint32_t    evidence_flags;
    uint32_t    quality_flags;
    uint64_t    state_entered_monotonic_us;
    uint64_t    latest_evidence_monotonic_us;
    uint64_t    continuous_duration_us;
    uint64_t    pressure_evidence_duration_us;
    uint64_t    source_snapshot_version;
    uint64_t    flow_result_version;
    uint64_t    pressure_result_version;
    uint64_t    volume_state_version;
    uint32_t    runtime_generation;
    uint32_t    config_version;
    uint32_t    algorithm_version;
    uint32_t    profile_version;
    int64_t     event_wall_time_s;
    TimeQuality event_time_quality;
} LeakDetectionResult;

#endif /* SWFPM_DOMAIN_LEAK_TYPES_H */
