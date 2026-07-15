#include "leak_tracker.h"
#include <string.h>

void LeakTracker_Init(LeakEvidenceTracker *t)
{
    memset(t, 0, sizeof(*t));
    t->phase = LEAK_PHASE_INACTIVE;
}

void LeakTracker_Evaluate(LeakEvidenceTracker *t,
                           bool entry_active,
                           bool clear_active,
                           bool source_usable,
                           uint64_t now_us,
                           uint64_t entry_duration_us,
                           uint64_t clear_duration_us,
                           uint64_t max_gap_us)
{
    if (!t) return;

    if (!source_usable) {
        if (t->phase == LEAK_PHASE_INACTIVE)
            return;

        if (t->phase != LEAK_PHASE_SUSPENDED) {
            t->suspended_from_phase = t->phase;
            t->phase = LEAK_PHASE_SUSPENDED;
        }

        if (t->last_usable_sample_us > 0 &&
            (now_us - t->last_usable_sample_us) > max_gap_us) {
            if (t->phase == LEAK_PHASE_SUSPENDED &&
                t->activation_count == 0) {
                LeakTracker_Init(t);
            }
        }
        return;
    }

    t->last_usable_sample_us = now_us;

    if (t->phase == LEAK_PHASE_SUSPENDED) {
        t->phase = t->suspended_from_phase;
        t->suspended_from_phase = LEAK_PHASE_INACTIVE;
    }

    switch (t->phase) {

    case LEAK_PHASE_INACTIVE:
        if (entry_active) {
            t->entry_start_us = now_us;
            t->phase = LEAK_PHASE_PENDING;
        }
        break;

    case LEAK_PHASE_PENDING:
        if (!entry_active) {
            if (clear_active) {
                t->clear_start_us = now_us;
                t->phase = LEAK_PHASE_CLEAR_PENDING;
            } else {
                LeakTracker_Init(t);
            }
        } else if ((now_us - t->entry_start_us) >= entry_duration_us) {
            t->active_since_us = now_us;
            t->activation_count++;
            t->phase = LEAK_PHASE_ACTIVE;
        }
        break;

    case LEAK_PHASE_ACTIVE:
        if (clear_active) {
            t->clear_start_us = now_us;
            t->phase = LEAK_PHASE_CLEAR_PENDING;
        }
        break;

    case LEAK_PHASE_CLEAR_PENDING:
        if (entry_active) {
            t->phase = LEAK_PHASE_ACTIVE;
        } else if (!clear_active) {
            t->phase = LEAK_PHASE_ACTIVE;
        } else if ((now_us - t->clear_start_us) >= clear_duration_us) {
            LeakTracker_Init(t);
        }
        break;

    case LEAK_PHASE_SUSPENDED:
        break;
    }
}
