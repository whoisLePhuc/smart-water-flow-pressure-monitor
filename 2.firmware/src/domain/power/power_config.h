#ifndef SWFPM_POWER_CONFIG_H
#define SWFPM_POWER_CONFIG_H

#include <stdint.h>

typedef struct {
    uint16_t vref_mv;
    uint8_t  divider_ratio;
    uint16_t low_threshold_mv;
    uint16_t critical_threshold_mv;
    uint16_t hysteresis_mv;
    uint16_t sample_period_s;
    uint8_t  stale_count_max;
} PowerConfig;

#define POWER_CONFIG_DEFAULT { 3300, 3, 3600, 3200, 100, 60, 3 }

#endif
