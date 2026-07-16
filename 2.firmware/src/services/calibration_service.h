#ifndef SWFPM_CALIBRATION_SERVICE_H
#define SWFPM_CALIBRATION_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/measurement/measurement_types.h"
#include "app_event_queue.h"
#include "data_repository.h"
#include "services/sensor_profile.h"

/* CalibrationService — temperature calibration owner.
 * Receives raw MAX timing, performs RTD-based conversion,
 * publishes TemperatureResult and EVT_TEMPERATURE_RESULT_READY.
 */

#define TEMP_RTD_TABLE_MAX_SIZE 64

/* Temperature channel binding: maps source to probe/reference ports */
typedef enum {
    TEMP_SOURCE_WATER,
    TEMP_SOURCE_METER_BODY,
    TEMP_SOURCE_AMBIENT,
    TEMP_SOURCE_REFERENCE_ONLY
} TemperatureSourceRole;

typedef struct {
    uint32_t source_id;
    TemperatureSourceRole role;
    uint8_t  probe_port;
    uint8_t  reference_port;
} TemperatureChannelBinding;

/* Processing status */
typedef enum {
    TEMP_OK,
    TEMP_INVALID_SAMPLE,
    TEMP_STALE_SAMPLE,
    TEMP_PROFILE_ERROR,
    TEMP_NUMERIC_ERROR,
    TEMP_INTERNAL_ERROR
} TemperatureProcessStatus;

/* Internal candidate (not public result) */
typedef struct {
    uint32_t source_id;
    TemperatureSourceRole source_role;
    int64_t  resistance_uohm;             /* Corrected resistance in µΩ */
    int32_t  unfiltered_temperature_mdeg_c;
    uint32_t processing_flags;
    ResultMetadata meta;
} TemperatureCandidate;

/* CalibrationService state */
typedef struct {
    AppEventQueue                 *event_queue;
    DataRepository                *repo;
    uint32_t                       generation;
    const TemperatureProfile      *active_profile;
    const CalibrationRecord       *active_cal;
    TemperatureChannelBinding      binding;

    /* Diagnostics */
    uint32_t accepted_count;
    uint32_t rejected_stale_count;
    uint32_t rejected_invalid_count;
    uint32_t numeric_error_count;
} CalibrationService;

/* ── Lifecycle ──────────────────────────────────────────── */

void calibration_service_init(CalibrationService *svc,
                               AppEventQueue *event_queue,
                               DataRepository *repo);

void calibration_service_set_profile(CalibrationService *svc,
                                      const TemperatureProfile *profile);

void calibration_service_set_calibration(CalibrationService *svc,
                                          const CalibrationRecord *cal);

/* ── Pure conversion ─────────────────────────────────────── */

TemperatureProcessStatus temperature_convert_raw(
    uint16_t probe_integer, uint16_t probe_fraction,
    uint16_t ref_integer,   uint16_t ref_fraction,
    const TemperatureProfile *profile,
    const CalibrationRecord *cal,
    TemperatureCandidate *candidate);

/* ── Stateful accept + publish ────────────────────────────── */

TemperatureProcessStatus calibration_service_accept_raw(
    CalibrationService *svc,
    uint16_t probe_integer, uint16_t probe_fraction,
    uint16_t ref_integer,   uint16_t ref_fraction,
    SourceEventToken *token);

#endif
