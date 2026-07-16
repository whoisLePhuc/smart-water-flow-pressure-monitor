#ifndef SWFPM_RUNTIME_SNAPSHOT_H
#define SWFPM_RUNTIME_SNAPSHOT_H

/* =================================================================
 * RuntimeSnapshot — full system state view
 * Owner: infrastructure/repositories
 *
 * This is the aggregate snapshot of all measurement results and
 * system state. It lives in infrastructure/repositories because
 * it is the data format managed by DataRepository / RepoWriteTxn.
 *
 * It includes only domain-owned type headers — no driver payloads.
 * ================================================================= */

#include "domain/common/metadata.h"
#include "domain/common/status.h"
#include "domain/measurement/measurement_types.h"
#include "domain/product/volume_types.h"
#include "domain/product/leak_types.h"
#include "domain/system/system_types.h"

typedef struct {
    uint32_t    schema_version;
    uint64_t    snapshot_version;
    uint64_t    publish_monotonic_us;
    int64_t     publish_wall_time_s;
    TimeQuality publish_time_quality;

    SystemModeContext    mode;
    OrthogonalStatusSet  statuses;

    TemperatureResult    temperature;
    FlowResult           flow;
    PressureResult       pressure;
    VolumeState          volume;
    LeakDetectionResult  leak;

    uint32_t    active_config_version;
    uint32_t    active_calibration_version;
    uint32_t    diagnostic_summary_flags;
} RuntimeSnapshot;

#endif /* SWFPM_RUNTIME_SNAPSHOT_H */
