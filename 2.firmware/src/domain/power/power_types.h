#ifndef SWFPM_DOMAIN_POWER_TYPES_H
#define SWFPM_DOMAIN_POWER_TYPES_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    POWER_STATE_UNKNOWN = 0,
    POWER_STATE_NORMAL,
    POWER_STATE_LOW,
    POWER_STATE_CRITICAL
} PowerHealth;

#define POWER_QUALITY_ADC_FAULT (1u << 0)
#define POWER_QUALITY_STALE     (1u << 1)

typedef struct {
    uint64_t sample_sequence;
    uint64_t sample_monotonic_us;
    uint16_t battery_mv;
    uint16_t raw_adc;
    uint8_t  quality_flags;
    bool     available;
    PowerHealth health;
} PowerSnapshot;

#endif
