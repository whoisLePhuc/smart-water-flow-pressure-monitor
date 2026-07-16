#ifndef SWFPM_DATA_MODEL_H
#define SWFPM_DATA_MODEL_H

/**
 * @deprecated Removed in Phase 11.
 *
 * This header is a compatibility re-export of all domain types.
 * It exists to support incremental migration of consumers from
 * the old God Header to domain-owned type headers.
 *
 * Include the specific domain headers instead:
 *   - "domain/common/metadata.h"       — DataValidity, ResultMetadata, etc.
 *   - "domain/common/status.h"         — OrthogonalStatusSet
 *   - "domain/measurement/measurement_types.h" — FlowResult, TemperatureResult, PressureResult
 *   - "domain/product/volume_types.h"  — VolumeState
 *   - "domain/product/leak_types.h"    — LeakDetectionResult, LeakState
 *   - "domain/system/system_types.h"   — SystemMode, SystemModeContext
 *   - "domain/connectivity/reporting_types.h"  — ReportingWindowConfig
 *   - "domain/connectivity/delivery_types.h"   — DeliveryOutcome
 *   - "infrastructure/event/event_id.h"         — EventId
 *   - "infrastructure/repositories/runtime_snapshot.h" — RuntimeSnapshot
 *
 * Migration task: Phase 3 include batches, Phase 9 full cleanup
 */

#include "domain/common/metadata.h"
#include "domain/common/status.h"
#include "domain/measurement/measurement_types.h"
#include "domain/product/volume_types.h"
#include "domain/product/leak_types.h"
#include "domain/system/system_types.h"
#include "domain/connectivity/reporting_types.h"
#include "domain/connectivity/delivery_types.h"
#include "infrastructure/event/event_id.h"
#include "infrastructure/repositories/runtime_snapshot.h"

#endif /* SWFPM_DATA_MODEL_H */
