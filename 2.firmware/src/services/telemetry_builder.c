#include "telemetry_builder.h"
#include <string.h>

void TelemetryBuilder_Init(TelemetryBuilder *tb)
{
    if (tb) { tb->next_sequence = 1; }
}

bool TelemetryBuilder_Build(TelemetryBuilder *tb,
                             const RuntimeSnapshot *snap,
                             uint8_t window_id,
                             int64_t slot_due_wall_s,
                             uint16_t slot_ordinal,
                             uint64_t build_monotonic_us,
                             int64_t build_wall_s,
                             uint8_t wall_time_quality,
                             uint32_t schedule_version,
                             TelemetryRecord *rec)
{
    if (!tb || !snap || !rec) return false;

    memset(rec, 0, sizeof(*rec));

    rec->record_sequence   = tb->next_sequence++;
    rec->schema_version    = TELEMETRY_SCHEMA_VERSION;
    rec->report_reason     = TELEMETRY_REASON_SCHEDULED;

    rec->schedule_version  = schedule_version;
    rec->window_id         = window_id;
    rec->slot_due_wall_s   = slot_due_wall_s;
    rec->slot_ordinal      = slot_ordinal;

    rec->source_snapshot_version = snap->snapshot_version;
    rec->snapshot_generation     = snap->mode.mode_generation;
    rec->build_monotonic_us      = build_monotonic_us;
    rec->build_wall_s            = build_wall_s;
    rec->wall_time_quality       = wall_time_quality;

    rec->flow_ul_per_s     = snap->flow.flow_ul_per_s;
    rec->flow_direction    = (uint8_t)snap->flow.direction;
    rec->temperature_mdeg_c = snap->temperature.temperature_mdeg_c;
    rec->pressure_pa       = snap->pressure.pressure_pa;
    rec->forward_volume_ul = snap->volume.forward_volume_ul;
    rec->reverse_volume_ul = snap->volume.reverse_volume_ul;
    rec->leak_state        = (uint8_t)snap->leak.state;
    rec->leak_eval_status  = (uint8_t)snap->leak.evaluation_status;
    rec->system_mode       = (uint8_t)snap->mode.current_mode;

    rec->config_version    = snap->active_config_version;
    rec->calibration_version = snap->active_calibration_version;
    rec->diagnostic_flags  = snap->diagnostic_summary_flags;

    return true;
}
