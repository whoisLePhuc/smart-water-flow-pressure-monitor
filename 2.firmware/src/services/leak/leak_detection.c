#include "services/leak/leak_detection.h"
#include <string.h>

void LeakDetection_Init(LeakDetectionService *svc, const LeakDetectionConfig *cfg, uint64_t now_us)
{
    memset(svc, 0, sizeof(*svc));
    if (cfg) svc->cfg = *cfg;
    svc->state = LEAK_STATE_NORMAL;
    svc->eval_status = LEAK_EVAL_NOT_READY;
    svc->primary_reason = LEAK_REASON_NONE;
    svc->severity = LEAK_SEVERITY_NONE;
    svc->state_version = 1;
    svc->state_entered_us = now_us;
    svc->runtime_generation = 1;
    svc->last_eval_us = now_us;

    LeakTracker_Init(&svc->continuous_tracker);
    LeakTracker_Init(&svc->burst_tracker);
    LeakTracker_Init(&svc->low_pressure_tracker);
    LeakTracker_Init(&svc->high_pressure_tracker);
}

static LeakUpdateStatus build_result(LeakDetectionService *svc, const LeakInputView *input)
{
    uint64_t now_us = input ? input->evaluation_monotonic_us : svc->last_eval_us;
    svc->last_eval_us = now_us;

    /* State machine transitions */

    /* Burst confirmed takes highest precedence */
    bool burst_active = (svc->burst_tracker.phase == LEAK_PHASE_ACTIVE);

    /* Continuous tracker phases */
    bool continuous_pending = (svc->continuous_tracker.phase == LEAK_PHASE_PENDING);
    bool continuous_active  = (svc->continuous_tracker.phase == LEAK_PHASE_ACTIVE);
    bool continuous_clearing = (svc->continuous_tracker.phase == LEAK_PHASE_CLEAR_PENDING);

    /* NORMAL -> transitions */
    if (svc->state == LEAK_STATE_NORMAL) {
        if (burst_active) {
            svc->state = LEAK_STATE_CONFIRMED;
            svc->primary_reason = LEAK_REASON_HIGH_FLOW_BURST;
            svc->severity = LEAK_SEVERITY_HIGH;
            svc->state_change_sequence++;
            svc->state_entered_us = now_us;
        } else if (continuous_active && !continuous_clearing) {
            svc->state = LEAK_STATE_SUSPECTED;
            svc->primary_reason = LEAK_REASON_CONTINUOUS_FLOW;
            svc->severity = LEAK_SEVERITY_LOW;
            svc->state_change_sequence++;
            svc->state_entered_us = now_us;
        }
    }

    /* SUSPECTED -> transitions */
    if (svc->state == LEAK_STATE_SUSPECTED) {
        if (burst_active) {
            svc->state = LEAK_STATE_CONFIRMED;
            svc->primary_reason = LEAK_REASON_HIGH_FLOW_BURST;
            svc->severity = LEAK_SEVERITY_HIGH;
            svc->state_change_sequence++;
            svc->state_entered_us = now_us;
        } else if (continuous_active && !continuous_clearing) {
            /* Stay SUSPECTED — severity may increase if long duration */
            if (svc->continuous_tracker.active_since_us > 0 &&
                (now_us - svc->continuous_tracker.active_since_us) >=
                svc->cfg.continuous_confirm_duration_us) {
                svc->state = LEAK_STATE_CONFIRMED;
                svc->severity = LEAK_SEVERITY_MEDIUM;
                svc->state_change_sequence++;
                svc->state_entered_us = now_us;
            }
        } else if (!continuous_pending && !continuous_active && !continuous_clearing &&
                   svc->continuous_tracker.phase == LEAK_PHASE_INACTIVE) {
            svc->state = LEAK_STATE_NORMAL;
            svc->primary_reason = LEAK_REASON_NONE;
            svc->severity = LEAK_SEVERITY_NONE;
            svc->state_change_sequence++;
            svc->state_entered_us = now_us;
        }
    }

    /* CONFIRMED -> clear */
    if (svc->state == LEAK_STATE_CONFIRMED) {
        if (svc->continuous_tracker.phase == LEAK_PHASE_INACTIVE &&
            svc->burst_tracker.phase == LEAK_PHASE_INACTIVE) {
            svc->state = LEAK_STATE_NORMAL;
            svc->primary_reason = LEAK_REASON_NONE;
            svc->severity = LEAK_SEVERITY_NONE;
            svc->state_change_sequence++;
            svc->state_entered_us = now_us;
        }
    }

    /* Evaluation status */
    if (svc->state_version == 1 && svc->diag_evals == 0) {
        svc->eval_status = LEAK_EVAL_NOT_READY;
    } else if (input && !input->flow_usable) {
        svc->eval_status = LEAK_EVAL_UNAVAILABLE;
    } else if (input && !input->pressure_usable && svc->cfg.pressure_assist_enabled) {
        svc->eval_status = LEAK_EVAL_DEGRADED;
    } else {
        svc->eval_status = LEAK_EVAL_ACTIVE;
    }

    svc->state_version++;
    svc->diag_updates++;
    return LEAK_UPDATE_RESULT_CHANGED;
}

LeakUpdateStatus LeakDetection_Evaluate(LeakDetectionService *svc, const LeakInputView *input)
{
    if (!svc || !input) return LEAK_UPDATE_INTERNAL_ERROR;

    svc->diag_evals++;

    /* Check duplicate */
    if (input->flow_result && input->sample_sequence > 0) {
        if (input->sample_sequence == svc->last_flow_sequence &&
            input->source_generation == svc->runtime_generation) {
            svc->diag_duplicates++;
            return LEAK_UPDATE_REJECTED_DUPLICATE;
        }
    }
    if (input->sample_sequence > 0)
        svc->last_flow_sequence = input->sample_sequence;

    /* Update trackers */
    uint64_t now = input->evaluation_monotonic_us;
    uint64_t max_gap = svc->cfg.maximum_evidence_gap_us;

    /* Continuous-flow tracker */
    bool cont_entry = input->flow_usable &&
                      input->flow_direction == FLOW_DIRECTION_FORWARD &&
                      input->flow_ul_per_s >= svc->cfg.continuous_entry_ul_per_s &&
                      input->flow_ul_per_s < svc->cfg.burst_entry_ul_per_s;
    bool cont_clear = input->flow_usable &&
                      input->flow_ul_per_s <= svc->cfg.continuous_clear_ul_per_s;

    LeakTracker_Evaluate(&svc->continuous_tracker,
                          cont_entry, cont_clear, input->flow_usable,
                          now, svc->cfg.continuous_suspect_duration_us,
                          svc->cfg.continuous_clear_duration_us, max_gap);

    /* Burst tracker */
    bool burst_entry = input->flow_usable &&
                       input->flow_direction == FLOW_DIRECTION_FORWARD &&
                       input->flow_ul_per_s >= svc->cfg.burst_entry_ul_per_s;
    bool burst_clear = input->flow_usable &&
                       input->flow_ul_per_s <= svc->cfg.burst_clear_ul_per_s;

    LeakTracker_Evaluate(&svc->burst_tracker,
                          burst_entry, burst_clear, input->flow_usable,
                          now, svc->cfg.burst_confirm_duration_us,
                          svc->cfg.burst_clear_duration_us, max_gap);

    /* Pressure trackers */
    bool low_p = input->pressure_usable &&
                 input->pressure_pa <= svc->cfg.low_pressure_entry_pa;
    bool low_p_clear = input->pressure_usable &&
                       input->pressure_pa >= svc->cfg.low_pressure_clear_pa;

    LeakTracker_Evaluate(&svc->low_pressure_tracker,
                          low_p, low_p_clear, input->pressure_usable,
                          now, svc->cfg.pressure_activation_duration_us,
                          svc->cfg.pressure_clear_duration_us, max_gap);

    bool high_p = input->pressure_usable &&
                  input->pressure_pa >= svc->cfg.high_pressure_entry_pa;
    bool high_p_clear = input->pressure_usable &&
                        input->pressure_pa <= svc->cfg.high_pressure_clear_pa;

    LeakTracker_Evaluate(&svc->high_pressure_tracker,
                          high_p, high_p_clear, input->pressure_usable,
                          now, svc->cfg.pressure_activation_duration_us,
                          svc->cfg.pressure_clear_duration_us, max_gap);

    /* Build evidence flags */
    svc->evidence_flags = 0;
    if (svc->continuous_tracker.phase == LEAK_PHASE_ACTIVE)
        svc->evidence_flags |= LEAK_EVIDENCE_CONTINUOUS_FLOW;
    if (svc->burst_tracker.phase == LEAK_PHASE_ACTIVE)
        svc->evidence_flags |= LEAK_EVIDENCE_HIGH_FLOW;
    if (svc->low_pressure_tracker.phase == LEAK_PHASE_ACTIVE ||
        svc->high_pressure_tracker.phase == LEAK_PHASE_ACTIVE)
        svc->evidence_flags |= LEAK_EVIDENCE_LOW_PRESSURE;
    if (svc->high_pressure_tracker.phase == LEAK_PHASE_ACTIVE)
        svc->evidence_flags |= LEAK_EVIDENCE_HIGH_PRESSURE;

    /* Quality flags */
    svc->quality_flags = 0;
    if (!input->flow_usable) svc->quality_flags |= (1u << 0);
    if (!input->pressure_usable) svc->quality_flags |= (1u << 1);

    if (!input->flow_usable)
        svc->diag_flow_unusable++;
    if (!input->pressure_usable)
        svc->diag_pressure_unusable++;

    return build_result(svc, input);
}

bool LeakDetection_ApplyConfig(LeakDetectionService *svc, const LeakDetectionConfig *cfg, uint64_t now_us)
{
    if (!svc || !cfg) return false;
    if (!LeakConfig_Validate(cfg, NULL, 0)) return false;

    svc->cfg = *cfg;

    /* Reset unconfirmed trackers */
    if (svc->state != LEAK_STATE_CONFIRMED) {
        LeakTracker_Init(&svc->continuous_tracker);
        LeakTracker_Init(&svc->burst_tracker);
    }
    LeakTracker_Init(&svc->low_pressure_tracker);
    LeakTracker_Init(&svc->high_pressure_tracker);

    svc->last_eval_us = now_us;
    return true;
}

void LeakDetection_Reset(LeakDetectionService *svc, uint32_t new_generation, uint64_t now_us)
{
    if (!svc) return;
    svc->state = LEAK_STATE_NORMAL;
    svc->eval_status = LEAK_EVAL_NOT_READY;
    svc->primary_reason = LEAK_REASON_NONE;
    svc->severity = LEAK_SEVERITY_NONE;
    svc->reason_flags = 0;
    svc->evidence_flags = 0;
    svc->quality_flags = 0;
    svc->state_change_sequence = 0;
    svc->state_entered_us = now_us;
    svc->runtime_generation = new_generation;

    LeakTracker_Init(&svc->continuous_tracker);
    LeakTracker_Init(&svc->burst_tracker);
    LeakTracker_Init(&svc->low_pressure_tracker);
    LeakTracker_Init(&svc->high_pressure_tracker);

    svc->last_flow_sequence = 0;
    svc->last_pressure_sequence = 0;
    svc->last_eval_us = now_us;
}
