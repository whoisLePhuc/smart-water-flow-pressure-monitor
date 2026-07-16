#ifndef SWFPM_PRESSURE_SERVICE_H
#define SWFPM_PRESSURE_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/measurement/measurement_types.h"
#include "app_event_queue.h"
#include "data_repository.h"
#include "services/sensor_profile.h"

typedef enum {
    PRESSURE_OK,
    PRESSURE_INVALID_RAW,
    PRESSURE_STATUS_INVALID,
    PRESSURE_MAPPING_ERROR,
    PRESSURE_PROFILE_ERROR,
    PRESSURE_NUMERIC_ERROR,
    PRESSURE_INTERNAL_ERROR
} PressureProcessStatus;

typedef struct {
    uint32_t source_id;
    int32_t  pressure_pa;
    uint32_t processing_flags;
    ResultMetadata meta;
} PressureCandidate;

typedef struct {
    AppEventQueue           *event_queue;
    DataRepository          *repo;
    uint32_t                 generation;
    const PressureProfile   *active_profile;
    const CalibrationRecord *active_cal;
    uint32_t accepted_count;
    uint32_t rejected_count;
} PressureService;

void pressure_service_init(PressureService *svc, AppEventQueue *eq, DataRepository *repo);
void pressure_service_set_profile(PressureService *svc, const PressureProfile *p);
void pressure_service_set_calibration(PressureService *svc, const CalibrationRecord *c);

PressureProcessStatus pressure_convert(uint32_t raw_u24, uint8_t status,
    const PressureProfile *profile, const CalibrationRecord *cal,
    PressureCandidate *candidate);

PressureProcessStatus pressure_service_accept_raw(PressureService *svc,
    uint32_t raw_u24, uint8_t status, SourceEventToken *token);

#endif
