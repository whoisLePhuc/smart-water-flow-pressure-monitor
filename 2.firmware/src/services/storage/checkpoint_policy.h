#ifndef SWFPM_CHECKPOINT_POLICY_H
#define SWFPM_CHECKPOINT_POLICY_H

#include <stdint.h>
#include <stdbool.h>
#include "services/volume/volume_accumulator.h"

typedef enum {
    CP_NONE,
    CP_VOLUME_THRESHOLD,
    CP_TIME_THRESHOLD,
    CP_FORCED
} CheckpointReason;

typedef struct {
    uint64_t max_uncheckpointed_ul;
    uint32_t max_interval_s;
    uint32_t min_spacing_s;
} CheckpointPolicyConfig;

typedef struct {
    CheckpointPolicyConfig cfg;
    uint64_t last_checkpoint_monotonic_us;
    bool     in_flight;
} CheckpointPolicy;

void CheckpointPolicy_Init(CheckpointPolicy *self, const CheckpointPolicyConfig *cfg);
CheckpointReason CheckpointPolicy_Evaluate(
    CheckpointPolicy *self,
    const VolumeAccumulator *acc,
    uint64_t now_monotonic_us);
void CheckpointPolicy_OnSubmitted(CheckpointPolicy *self);
void CheckpointPolicy_OnCompleted(CheckpointPolicy *self, uint64_t now_monotonic_us);

#endif
