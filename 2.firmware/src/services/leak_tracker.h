#ifndef SWFPM_LEAK_TRACKER_H
#define SWFPM_LEAK_TRACKER_H

#include <stdint.h>
#include <stdbool.h>
#include "infrastructure/event/data_model.h"

/* Evidence phases per canonical model §8.5 */
typedef enum {
    LEAK_PHASE_INACTIVE,
    LEAK_PHASE_PENDING,
    LEAK_PHASE_ACTIVE,
    LEAK_PHASE_CLEAR_PENDING,
    LEAK_PHASE_SUSPENDED
} LeakEvidencePhase;

typedef struct {
    LeakEvidencePhase phase;
    LeakEvidencePhase suspended_from_phase;
    uint64_t entry_start_us;
    uint64_t active_since_us;
    uint64_t clear_start_us;
    uint64_t last_usable_sample_us;
    uint64_t last_source_sequence;
    uint64_t last_result_version;
    uint32_t source_generation;
    uint32_t activation_count;
} LeakEvidenceTracker;

void LeakTracker_Init(LeakEvidenceTracker *t);

/* Update tracker with new entry/clear condition.
 * entry_active: true if entry condition met
 * clear_active: true if clear condition met
 * source_usable: true if input valid/fresh
 * now_us: current monotonic time
 * entry_duration_us: time in PENDING before →ACTIVE
 * clear_duration_us: time in CLEAR_PENDING before →INACTIVE
 * max_gap_us: max allowed gap before unconfirmed reset */
void LeakTracker_Evaluate(LeakEvidenceTracker *t,
                           bool entry_active,
                           bool clear_active,
                           bool source_usable,
                           uint64_t now_us,
                           uint64_t entry_duration_us,
                           uint64_t clear_duration_us,
                           uint64_t max_gap_us);

#endif
