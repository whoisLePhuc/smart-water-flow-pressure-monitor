#ifndef SWFPM_LEAK_CONFIG_H
#define SWFPM_LEAK_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/product/leak_types.h"
#include "domain/measurement/measurement_types.h"
#include "domain/product/volume_types.h"
#include "domain/common/metadata.h"

#define LEAK_ALGORITHM_VERSION  1u

/* TEST_ONLY flag — production profiles MUST NOT use TEST_ONLY */
#define LEAK_CONFIG_FLAG_TEST_ONLY  0x80000000u

typedef struct {
    uint32_t schema_version;
    uint32_t config_version;
    uint32_t algorithm_version;
    uint32_t profile_version;
    uint32_t flags;

    int64_t  continuous_entry_ul_per_s;
    int64_t  continuous_clear_ul_per_s;
    uint64_t continuous_suspect_duration_us;
    uint64_t continuous_confirm_duration_us;
    uint64_t continuous_clear_duration_us;

    int64_t  burst_entry_ul_per_s;
    int64_t  burst_clear_ul_per_s;
    uint64_t burst_confirm_duration_us;
    uint64_t burst_clear_duration_us;

    bool     pressure_assist_enabled;
    int32_t  low_pressure_entry_pa;
    int32_t  low_pressure_clear_pa;
    int32_t  high_pressure_entry_pa;
    int32_t  high_pressure_clear_pa;
    uint64_t pressure_activation_duration_us;
    uint64_t pressure_clear_duration_us;

    bool     pressure_drop_enabled;
    int32_t  pressure_drop_entry_pa;
    int32_t  pressure_drop_clear_pa;
    uint64_t pressure_drop_window_us;
    uint32_t pressure_drop_min_samples;

    uint64_t maximum_flow_age_us;
    uint64_t maximum_pressure_age_us;
    uint64_t maximum_evidence_gap_us;
    uint64_t correlation_window_us;
    uint64_t confirmed_clear_duration_us;
    uint64_t evaluation_period_us;
} LeakDetectionConfig;

bool LeakConfig_Validate(const LeakDetectionConfig *cfg, char *error_out, uint16_t error_size);

/* Create test-only default config. These are NOT production-qualified. */
void LeakConfig_GetTestDefaults(LeakDetectionConfig *cfg);

#endif
