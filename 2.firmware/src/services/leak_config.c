#include "leak_config.h"
#include <string.h>
#include <stdio.h>

bool LeakConfig_Validate(const LeakDetectionConfig *cfg, char *error_out, uint16_t error_size)
{
    if (error_out && error_size) error_out[0] = 0;

    if (!cfg) {
        if (error_out && error_size > 0)
            snprintf(error_out, error_size, "null config");
        return false;
    }

    if (cfg->schema_version == 0 || cfg->algorithm_version == 0) {
        if (error_out && error_size > 0)
            snprintf(error_out, error_size, "version fields must be nonzero");
        return false;
    }

    if (cfg->continuous_clear_ul_per_s >= cfg->continuous_entry_ul_per_s) {
        if (error_out && error_size > 0)
            snprintf(error_out, error_size, "continuous clear >= entry");
        return false;
    }

    if (cfg->continuous_entry_ul_per_s >= cfg->burst_entry_ul_per_s) {
        if (error_out && error_size > 0)
            snprintf(error_out, error_size, "continuous entry >= burst entry");
        return false;
    }

    if (cfg->burst_clear_ul_per_s >= cfg->burst_entry_ul_per_s) {
        if (error_out && error_size > 0)
            snprintf(error_out, error_size, "burst clear >= burst entry");
        return false;
    }

    if (cfg->continuous_suspect_duration_us == 0 ||
        cfg->continuous_confirm_duration_us == 0 ||
        cfg->burst_confirm_duration_us == 0) {
        if (error_out && error_size > 0)
            snprintf(error_out, error_size, "duration must be nonzero");
        return false;
    }

    if (cfg->continuous_confirm_duration_us < cfg->continuous_suspect_duration_us) {
        if (error_out && error_size > 0)
            snprintf(error_out, error_size, "confirm < suspect duration");
        return false;
    }

    if (cfg->maximum_flow_age_us == 0 || cfg->maximum_pressure_age_us == 0) {
        if (error_out && error_size > 0)
            snprintf(error_out, error_size, "max age must be nonzero");
        return false;
    }

    if (cfg->low_pressure_entry_pa >= cfg->high_pressure_entry_pa) {
        if (error_out && error_size > 0)
            snprintf(error_out, error_size, "low entry >= high entry");
        return false;
    }

    return true;
}

void LeakConfig_GetTestDefaults(LeakDetectionConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->schema_version  = 1;
    cfg->config_version  = 1;
    cfg->algorithm_version = LEAK_ALGORITHM_VERSION;
    cfg->profile_version = 0;
    cfg->flags = LEAK_CONFIG_FLAG_TEST_ONLY;

    /* Continuous-flow: 5 L/min entry, 2 L/min clear */
    cfg->continuous_entry_ul_per_s       = 83333;
    cfg->continuous_clear_ul_per_s       = 33333;
    cfg->continuous_suspect_duration_us  = 30000000;
    cfg->continuous_confirm_duration_us  = 120000000;
    cfg->continuous_clear_duration_us    = 60000000;

    /* Burst: 20 L/min entry, 10 L/min clear */
    cfg->burst_entry_ul_per_s            = 333333;
    cfg->burst_clear_ul_per_s            = 166666;
    cfg->burst_confirm_duration_us       = 5000000;
    cfg->burst_clear_duration_us         = 30000000;

    cfg->pressure_assist_enabled         = false;
    cfg->low_pressure_entry_pa           = 100000;
    cfg->low_pressure_clear_pa           = 150000;
    cfg->high_pressure_entry_pa          = 800000;
    cfg->high_pressure_clear_pa          = 700000;
    cfg->pressure_activation_duration_us = 30000000;
    cfg->pressure_clear_duration_us      = 60000000;

    cfg->pressure_drop_enabled           = false;
    cfg->pressure_drop_entry_pa          = 50000;
    cfg->pressure_drop_clear_pa          = 20000;
    cfg->pressure_drop_window_us         = 60000000;
    cfg->pressure_drop_min_samples       = 3;

    cfg->maximum_flow_age_us             = 60000000;
    cfg->maximum_pressure_age_us         = 60000000;
    cfg->maximum_evidence_gap_us         = 120000000;
    cfg->correlation_window_us           = 30000000;
    cfg->confirmed_clear_duration_us     = 120000000;
    cfg->evaluation_period_us            = 1000000;
}
