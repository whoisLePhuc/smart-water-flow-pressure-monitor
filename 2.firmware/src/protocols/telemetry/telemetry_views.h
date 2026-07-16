#ifndef SWFPM_TELEMETRY_VIEWS_H
#define SWFPM_TELEMETRY_VIEWS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int64_t  flow_ul_per_s;
    uint8_t  flow_direction;
    int32_t  temperature_mdeg_c;
    int32_t  pressure_pa;
    uint8_t  data_quality;
} TelemetryMeasurementView;

typedef struct {
    uint64_t forward_volume_ul;
    uint64_t reverse_volume_ul;
    uint8_t  leak_state;
    uint8_t  leak_eval_status;
} TelemetryProductView;

typedef struct {
    bool     available;
    uint16_t battery_mv;
} TelemetryPowerView;

typedef struct {
    uint8_t  system_mode;
    uint32_t diagnostic_flags;
    uint32_t config_version;
    uint32_t calibration_version;
} TelemetrySystemView;

#endif
