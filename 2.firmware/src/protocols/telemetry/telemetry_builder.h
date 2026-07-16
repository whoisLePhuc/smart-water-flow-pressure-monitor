#ifndef SWFPM_TELEMETRY_BUILDER_H
#define SWFPM_TELEMETRY_BUILDER_H

#include <stdint.h>
#include <stdbool.h>
#include "protocols/telemetry/telemetry_record.h"
#include "infrastructure/repositories/runtime_snapshot.h"

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
