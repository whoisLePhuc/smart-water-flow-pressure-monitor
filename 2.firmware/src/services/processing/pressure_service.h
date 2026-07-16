#ifndef SWFPM_PRESSURE_SERVICE_H
#define SWFPM_PRESSURE_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/measurement/measurement_types.h"
#include "infrastructure/queues/app_event_queue.h"
#include "infrastructure/repositories/repo_transaction.h"
#include "services/configuration/sensor_profile.h"

typedef enum {
    PRESSURE_OK,             /* Result was computed and written successfully. */
    PRESSURE_INVALID_RAW,    /* Raw 24-bit code is outside the accepted range. */
    PRESSURE_STATUS_INVALID, /* ZSSC status forbids production acceptance. */
    PRESSURE_MAPPING_ERROR,  /* Transfer-function mapping is inconsistent. */
    PRESSURE_PROFILE_ERROR,  /* Profile or calibration is unavailable/invalid. */
    PRESSURE_NUMERIC_ERROR,  /* Checked intermediate arithmetic failed. */
    PRESSURE_INTERNAL_ERROR  /* Service invariant or transaction write failed. */
} PressureProcessStatus;

typedef struct {
    uint32_t source_id;
    int32_t  pressure_pa;
    uint32_t processing_flags;
    ResultMetadata meta;
} PressureCandidate;

typedef struct {
    AppEventQueue *event_queue; /* Borrowed; owner must outlive the service. */
    uint32_t generation;        /* Profile changes invalidate pending samples. */
    const PressureProfile *active_profile; /* Borrowed; NULL until configured. */
    const CalibrationRecord *active_cal;   /* Borrowed; NULL until configured. */
    uint32_t accepted_count; /* Monotonic diagnostic counter. */
    uint32_t rejected_count; /* Reset when the active profile changes. */
} PressureService;

void pressure_service_init(PressureService *svc, AppEventQueue *eq);
void pressure_service_set_profile(PressureService *svc, const PressureProfile *p);
void pressure_service_set_calibration(PressureService *svc, const CalibrationRecord *c);

PressureProcessStatus pressure_convert(uint32_t raw_u24, uint8_t status,
    const PressureProfile *profile, const CalibrationRecord *cal,
    PressureCandidate *candidate);

// Converts and writes pressure through txn. The caller owns txn lifecycle;
// active_profile and active_cal must be configured before the call.
PressureProcessStatus pressure_service_accept_raw(PressureService *svc,
    uint32_t raw_u24, uint8_t status, RepoWriteTxn *txn,
    uint32_t correlation_id);

#endif
