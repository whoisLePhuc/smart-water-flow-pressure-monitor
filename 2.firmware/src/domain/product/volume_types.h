#ifndef SWFPM_DOMAIN_VOLUME_TYPES_H
#define SWFPM_DOMAIN_VOLUME_TYPES_H


#include <stdint.h>
#include "domain/common/metadata.h"


typedef struct {
    uint64_t    state_version;
    uint64_t    total_volume_ul;             /* microlitres (deprecated — kept for ABI compat; use forward+reverse) */
    uint64_t    forward_volume_ul;           /* total forward volume, monotonic */
    uint64_t    reverse_volume_ul;           /* total reverse volume, monotonic */
    uint64_t    forward_remainder;           /* fractional remainder carry (< 1_000_000) */
    uint64_t    reverse_remainder;           /* fractional remainder carry (< 1_000_000) */
    uint64_t    last_consumed_flow_sequence;
    uint64_t    last_sample_monotonic_us;
    uint64_t    anchor_sample_monotonic_us;  /* 0 = no anchor */
    int64_t     anchor_flow_ul_per_s;
    uint32_t    source_generation;
    uint32_t    binding_id;
    uint64_t    updated_monotonic_us;
    uint64_t    checkpointed_forward_ul;
    uint64_t    checkpointed_reverse_ul;
    uint64_t    checkpoint_sequence;
    uint32_t    config_version;
    uint32_t    flags;
} VolumeState;

#endif /* SWFPM_DOMAIN_VOLUME_TYPES_H */
