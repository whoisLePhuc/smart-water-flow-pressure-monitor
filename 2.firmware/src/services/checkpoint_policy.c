#include "checkpoint_policy.h"
#include <string.h>

void CheckpointPolicy_Init(CheckpointPolicy *self, const CheckpointPolicyConfig *cfg)
{
    memset(self, 0, sizeof(*self));
    if (cfg) self->cfg = *cfg;
}

CheckpointReason CheckpointPolicy_Evaluate(
    CheckpointPolicy *self,
    const VolumeAccumulator *acc,
    uint64_t now_us)
{
    if (!self || !acc)
        return CP_NONE;

    if (self->in_flight)
        return CP_NONE;

    const VolumeState *st = VolumeAccumulator_GetState(acc);

    /* Check volume threshold */
    uint64_t uncheckpointed_forward = 0;
    uint64_t uncheckpointed_reverse = 0;

    if (st->forward_volume_ul >= st->checkpointed_forward_ul)
        uncheckpointed_forward = st->forward_volume_ul - st->checkpointed_forward_ul;
    if (st->reverse_volume_ul >= st->checkpointed_reverse_ul)
        uncheckpointed_reverse = st->reverse_volume_ul - st->checkpointed_reverse_ul;

    uint64_t total_uncheckpointed = uncheckpointed_forward + uncheckpointed_reverse;
    if (total_uncheckpointed >= self->cfg.max_uncheckpointed_ul)
        return CP_VOLUME_THRESHOLD;

    /* Check time threshold */
    if (self->last_checkpoint_monotonic_us > 0) {
        uint64_t elapsed = now_us - self->last_checkpoint_monotonic_us;
        if (elapsed >= (uint64_t)self->cfg.max_interval_s * 1000000ULL)
            return CP_TIME_THRESHOLD;
    }

    return CP_NONE;
}

void CheckpointPolicy_OnSubmitted(CheckpointPolicy *self)
{
    if (self)
        self->in_flight = true;
}

void CheckpointPolicy_OnCompleted(CheckpointPolicy *self, uint64_t now_us)
{
    if (!self) return;
    self->in_flight = false;
    self->last_checkpoint_monotonic_us = now_us;
}
