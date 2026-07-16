#ifndef SWFPM_VOLUME_ACCUMULATOR_H
#define SWFPM_VOLUME_ACCUMULATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/product/leak_types.h"
#include "domain/product/volume_types.h"
#include "domain/measurement/measurement_types.h"
#include "domain/common/metadata.h"

/* =================================================================
 * Volume status codes (T010)
 * ================================================================= */

typedef enum {
    VOLUME_OK,
    VOLUME_ANCHORED,                        /* First sample — anchor set, no volume */
    VOLUME_ZERO_INTERVAL,                   /* dt == 0, no volume */
    VOLUME_REJECTED_NON_PRODUCTION,         /* purpose != PRODUCTION */
    VOLUME_REJECTED_INVALID,                /* valid == false */
    VOLUME_REJECTED_STALE,                  /* freshness == STALE */
    VOLUME_REJECTED_DUPLICATE,              /* identity already consumed */
    VOLUME_REJECTED_GENERATION,             /* source generation mismatch */
    VOLUME_REJECTED_BINDING,                /* binding incompatibility */
    VOLUME_REJECTED_TIME,                   /* backward/large-gap timestamp */
    VOLUME_OVERFLOW,                        /* counter/integer overflow */
    VOLUME_INTERNAL_ERROR
} VolumeConsumeStatus;

typedef enum {
    VOLUME_CHECKPOINT_IDLE,
    VOLUME_CHECKPOINT_TRIGGERED,
    VOLUME_CHECKPOINT_IN_FLIGHT,
    VOLUME_CHECKPOINT_FAILED
} VolumeCheckpointStatus;

/* =================================================================
 * VolumeConfig (T005)
 * ================================================================= */

typedef struct {
    uint32_t    config_version;
    uint64_t    maximum_integration_gap_us;  /* max dt before re-anchor */
    uint64_t    max_uncheckpointed_volume_ul;
    uint32_t    max_interval_s;              /* max seconds between checkpoints */
    uint32_t    min_spacing_s;               /* min seconds between checkpoints */
} VolumeConfig;

/* =================================================================
 * CheckpointCandidate (logical, before encoding)
 * ================================================================= */

typedef struct {
    uint64_t    checkpoint_sequence;
    uint64_t    state_version;
    uint64_t    forward_volume_ul;
    uint64_t    reverse_volume_ul;
    uint64_t    forward_remainder;
    uint64_t    reverse_remainder;
    uint64_t    last_consumed_flow_sequence;
    uint64_t    last_sample_monotonic_us;
    uint32_t    source_generation;
    uint32_t    binding_id;
} CheckpointCandidate;

/* =================================================================
 * VolumeAccumulator state (visible for static allocation)
 * ================================================================= */

typedef struct {
    VolumeConfig  config;

    /* Published VolumeState (kept in sync on every state change) */
    VolumeState   state;

    /* Private integration anchor */
    bool          anchor_valid;
    uint64_t      anchor_sample_us;
    int64_t       anchor_flow_ul_per_s;
    uint32_t      anchor_source_generation;
    uint32_t      anchor_binding_id;

    /* Private remainder state (forward/reverse fractional carry) */
    uint64_t      forward_rem;
    uint64_t      reverse_rem;

    /* Exactly-once watermark */
    uint32_t      last_gen;
    uint64_t      last_seq;
    uint64_t      last_ver;

    /* Runtime generation and diagnostics */
    uint32_t      source_generation;
    uint64_t      diag_seen;
    uint64_t      diag_consumed;
    uint64_t      diag_nonprod_rejected;
    uint64_t      diag_invalid_rejected;
    uint64_t      diag_duplicate_rejected;
    uint64_t      diag_stale_rejected;
    uint64_t      diag_gap_reanchored;
    uint64_t      diag_gen_reanchored;
    uint64_t      diag_time_faults;
} VolumeAccumulator;

/* =================================================================
 * Volume accumulator public API
 * ================================================================= */

void VolumeAccumulator_Init(VolumeAccumulator *self, const VolumeConfig *config);

/* Consume one FlowResult. Returns terminal outcome. */
VolumeConsumeStatus VolumeAccumulator_Consume(
    VolumeAccumulator *self,
    const FlowResult *flow);

/* Read current committed VolumeState — valid until next Consume call. */
const VolumeState* VolumeAccumulator_GetState(const VolumeAccumulator *self);

/* Prepare immutable checkpoint candidate from current committed state. */
bool VolumeAccumulator_PrepareCheckpoint(
    const VolumeAccumulator *self,
    CheckpointCandidate *candidate);

/* Update checkpointed reference after durable success. */
void VolumeAccumulator_OnCheckpointSuccess(
    VolumeAccumulator *self,
    const CheckpointCandidate *candidate);

/* Restore volume from persistent state after boot. */
void VolumeAccumulator_Restore(
    VolumeAccumulator *self,
    const VolumeState *restored);

/* Clear integration anchor (post-restore or policy change). */
void VolumeAccumulator_ClearAnchor(VolumeAccumulator *self);

#endif /* SWFPM_VOLUME_ACCUMULATOR_H */
