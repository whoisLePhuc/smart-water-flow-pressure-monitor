#include "services/volume/volume_accumulator.h"
#include <string.h>

#define VOLUME_TIME_SCALE_US_PER_S  UINT64_C(1000000)


static bool checked_mul_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a > 0 && b > UINT64_MAX / a) return true;
    *out = a * b;
    return false;
}

static bool checked_add_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a > UINT64_MAX - b) return true;
    *out = a + b;
    return false;
}

static void state_bump(VolumeAccumulator *self, uint64_t now_us)
{
    self->state.state_version++;
    self->state.updated_monotonic_us = now_us;
}

static void state_publish_identity(VolumeAccumulator *self,
                                   const ResultMetadata *meta,
                                   uint64_t now_us)
{
    self->state.last_consumed_flow_sequence = meta->sample_sequence;
    self->state.last_sample_monotonic_us = now_us;
    self->state.anchor_sample_monotonic_us = self->anchor_sample_us;
    self->state.anchor_flow_ul_per_s = self->anchor_flow_ul_per_s;
    self->state.source_generation = meta->source_generation;
    self->state.binding_id = meta->binding.binding_id;
    self->state.forward_remainder = self->forward_rem;
    self->state.reverse_remainder = self->reverse_rem;
}


void VolumeAccumulator_Init(VolumeAccumulator *self, const VolumeConfig *config)
{
    memset(self, 0, sizeof(*self));
    if (config) {
        self->config = *config;
        self->state.config_version = config->config_version;
    } else {
        self->config.maximum_integration_gap_us = 5000000;
        self->config.max_uncheckpointed_volume_ul = 100000;
        self->config.max_interval_s = 3600;
        self->config.min_spacing_s = 60;
    }
    self->state.state_version = 1;
}

VolumeConsumeStatus VolumeAccumulator_Consume(
    VolumeAccumulator *self,
    const FlowResult *flow)
{
    if (!self || !flow)
        return VOLUME_INTERNAL_ERROR;

    self->diag_seen++;
    const ResultMetadata *m = &flow->meta;

    if (m->purpose != MEAS_PURPOSE_PRODUCTION ||
        m->origin  != DATA_ORIGIN_LIVE_DEVICE ||
        m->provenance != PROVENANCE_MEASURED) {
        self->diag_nonprod_rejected++;
        return VOLUME_REJECTED_NON_PRODUCTION;
    }
    if (m->validity != DATA_VALID) {
        self->diag_invalid_rejected++;
        return VOLUME_REJECTED_INVALID;
    }
    if (m->freshness != DATA_FRESH) {
        self->diag_stale_rejected++;
        return VOLUME_REJECTED_STALE;
    }
    if (m->acceptance != DATA_ACCEPTED)
        return VOLUME_REJECTED_INVALID;

    if (self->diag_consumed > 0 &&
        m->source_generation != self->source_generation) {
        self->diag_stale_rejected++;
        return VOLUME_REJECTED_GENERATION;
    }

    if (self->diag_consumed > 0 &&
        m->source_generation == self->last_gen &&
        m->sample_sequence   == self->last_seq &&
        m->result_version    == self->last_ver) {
        self->diag_duplicate_rejected++;
        return VOLUME_REJECTED_DUPLICATE;
    }

    uint64_t now_us = m->sample_monotonic_us;

    if (!self->anchor_valid) {
        self->anchor_sample_us         = now_us;
        self->anchor_flow_ul_per_s     = flow->flow_ul_per_s;
        self->anchor_source_generation = m->source_generation;
        self->anchor_binding_id        = m->binding.binding_id;
        self->anchor_valid             = true;

        self->source_generation = m->source_generation;
        self->last_gen = m->source_generation;
        self->last_seq = m->sample_sequence;
        self->last_ver = m->result_version;
        self->diag_consumed++;
        state_publish_identity(self, m, now_us);
        state_bump(self, now_us);
        return VOLUME_ANCHORED;
    }

    if (now_us == self->anchor_sample_us) {
        self->source_generation = m->source_generation;
        self->last_gen = m->source_generation;
        self->last_seq = m->sample_sequence;
        self->last_ver = m->result_version;
        self->diag_consumed++;
        state_publish_identity(self, m, now_us);
        state_bump(self, now_us);
        return VOLUME_ZERO_INTERVAL;
    }

    if (now_us < self->anchor_sample_us) {
        self->diag_time_faults++;
        self->state.flags |= (1u << 2);
        self->anchor_sample_us         = now_us;
        self->anchor_flow_ul_per_s     = flow->flow_ul_per_s;
        self->anchor_source_generation = m->source_generation;
        self->anchor_binding_id        = m->binding.binding_id;
        self->source_generation = m->source_generation;
        self->last_gen = m->source_generation;
        self->last_seq = m->sample_sequence;
        self->last_ver = m->result_version;
        self->diag_consumed++;
        state_publish_identity(self, m, now_us);
        state_bump(self, now_us);
        return VOLUME_REJECTED_TIME;
    }

    uint64_t dt_us = now_us - self->anchor_sample_us;
    if (dt_us > self->config.maximum_integration_gap_us) {
        self->diag_gap_reanchored++;
        self->state.flags |= (1u << 3);
        self->anchor_sample_us         = now_us;
        self->anchor_flow_ul_per_s     = flow->flow_ul_per_s;
        self->anchor_source_generation = m->source_generation;
        self->anchor_binding_id        = m->binding.binding_id;
        self->source_generation = m->source_generation;
        self->last_gen = m->source_generation;
        self->last_seq = m->sample_sequence;
        self->last_ver = m->result_version;
        self->diag_consumed++;
        state_publish_identity(self, m, now_us);
        state_bump(self, now_us);
        return VOLUME_REJECTED_TIME;
    }

    uint64_t abs_flow;
    bool negative = (self->anchor_flow_ul_per_s < 0);
    if (negative)
        abs_flow = (uint64_t)(-self->anchor_flow_ul_per_s);
    else
        abs_flow = (uint64_t)(self->anchor_flow_ul_per_s);

    uint64_t numerator;
    if (checked_mul_u64(abs_flow, dt_us, &numerator))
        return VOLUME_OVERFLOW;

    uint64_t prev_rem = negative ? self->reverse_rem : self->forward_rem;
    if (checked_add_u64(numerator, prev_rem, &numerator))
        return VOLUME_OVERFLOW;

    uint64_t delta_ul = numerator / VOLUME_TIME_SCALE_US_PER_S;
    uint64_t new_rem  = numerator % VOLUME_TIME_SCALE_US_PER_S;

    if (delta_ul > 0) {
        if (negative) {
            if (checked_add_u64(self->state.reverse_volume_ul, delta_ul,
                                &self->state.reverse_volume_ul))
                return VOLUME_OVERFLOW;
            self->reverse_rem = new_rem;
        } else if (self->anchor_flow_ul_per_s > 0) {
            if (checked_add_u64(self->state.forward_volume_ul, delta_ul,
                                &self->state.forward_volume_ul))
                return VOLUME_OVERFLOW;
            self->forward_rem = new_rem;
        }
    } else {
        if (negative)
            self->reverse_rem = new_rem;
        else if (self->anchor_flow_ul_per_s > 0)
            self->forward_rem = new_rem;
    }

    self->anchor_sample_us         = now_us;
    self->anchor_flow_ul_per_s     = flow->flow_ul_per_s;
    self->anchor_source_generation = m->source_generation;
    self->anchor_binding_id        = m->binding.binding_id;

    self->last_gen = m->source_generation;
    self->last_seq = m->sample_sequence;
    self->last_ver = m->result_version;
    self->diag_consumed++;
    state_publish_identity(self, m, now_us);
    state_bump(self, now_us);

    return VOLUME_OK;
}

const VolumeState* VolumeAccumulator_GetState(const VolumeAccumulator *self)
{
    return &self->state;
}

bool VolumeAccumulator_PrepareCheckpoint(
    const VolumeAccumulator *self,
    CheckpointCandidate *candidate)
{
    if (!self || !candidate)
        return false;

    memset(candidate, 0, sizeof(*candidate));
    candidate->checkpoint_sequence         = self->state.checkpoint_sequence + 1;
    candidate->state_version               = self->state.state_version;
    candidate->forward_volume_ul           = self->state.forward_volume_ul;
    candidate->reverse_volume_ul           = self->state.reverse_volume_ul;
    candidate->forward_remainder           = self->forward_rem;
    candidate->reverse_remainder           = self->reverse_rem;
    candidate->last_consumed_flow_sequence = self->state.last_consumed_flow_sequence;
    candidate->last_sample_monotonic_us    = self->state.updated_monotonic_us;
    candidate->source_generation           = self->source_generation;
    candidate->binding_id                  = self->anchor_binding_id;

    return true;
}

void VolumeAccumulator_OnCheckpointSuccess(
    VolumeAccumulator *self,
    const CheckpointCandidate *candidate)
{
    if (!self || !candidate)
        return;

    self->state.checkpointed_forward_ul = candidate->forward_volume_ul;
    self->state.checkpointed_reverse_ul = candidate->reverse_volume_ul;
    self->state.checkpoint_sequence     = candidate->checkpoint_sequence;
}

void VolumeAccumulator_Restore(
    VolumeAccumulator *self,
    const VolumeState *restored)
{
    if (!self || !restored)
        return;

    self->state.forward_volume_ul       = restored->forward_volume_ul;
    self->state.reverse_volume_ul       = restored->reverse_volume_ul;
    self->forward_rem                   = restored->forward_remainder;
    self->reverse_rem                   = restored->reverse_remainder;
    self->state.checkpointed_forward_ul = restored->checkpointed_forward_ul;
    self->state.checkpointed_reverse_ul = restored->checkpointed_reverse_ul;
    self->state.checkpoint_sequence     = restored->checkpoint_sequence;
    self->state.state_version           = restored->state_version + 1;
    self->source_generation             = restored->source_generation;
    self->state.flags                   = restored->flags | (1u << 1);
    self->anchor_valid                  = false;
}

void VolumeAccumulator_ClearAnchor(VolumeAccumulator *self)
{
    if (self)
        self->anchor_valid = false;
}
