#ifndef SWFPM_TELEMETRY_BUILDER_H
#define SWFPM_TELEMETRY_BUILDER_H

#include <stdint.h>
#include <stdbool.h>
#include "infrastructure/event/data_model.h"

#define TELEMETRY_SCHEMA_VERSION 1u
#define TELEMETRY_REASON_SCHEDULED 1u

typedef struct {
    uint64_t record_sequence;
    uint16_t schema_version;
    uint16_t report_reason;

    uint32_t schedule_version;
    uint8_t  window_id;
    int64_t  slot_due_wall_s;
    uint16_t slot_ordinal;

    uint64_t source_snapshot_version;
    uint32_t snapshot_generation;
    uint64_t build_monotonic_us;
    int64_t  build_wall_s;
    uint8_t  wall_time_quality;

    int64_t  flow_ul_per_s;
    uint8_t  flow_direction;
    int32_t  temperature_mdeg_c;
    int32_t  pressure_pa;
    uint64_t forward_volume_ul;
    uint64_t reverse_volume_ul;
    uint8_t  leak_state;
    uint8_t  leak_eval_status;
    uint8_t  system_mode;

    uint32_t config_version;
    uint32_t calibration_version;
    uint32_t diagnostic_flags;
} TelemetryRecord;

typedef struct {
    uint64_t next_sequence;
} TelemetryBuilder;

void TelemetryBuilder_Init(TelemetryBuilder *tb);

bool TelemetryBuilder_Build(TelemetryBuilder *tb,
                             const RuntimeSnapshot *snapshot,
                             uint8_t window_id,
                             int64_t slot_due_wall_s,
                             uint16_t slot_ordinal,
                             uint64_t build_monotonic_us,
                             int64_t build_wall_s,
                             uint8_t wall_time_quality,
                             uint32_t schedule_version,
                             TelemetryRecord *record_out);

#endif
