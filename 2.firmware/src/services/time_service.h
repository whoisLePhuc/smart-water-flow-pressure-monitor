#ifndef SWFPM_TIME_SERVICE_H
#define SWFPM_TIME_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "infrastructure/event/data_model.h"

typedef struct {
    int64_t  wall_time_s;
    bool     valid;
    SystemTimeQuality quality;
    uint8_t  active_source;
    uint32_t time_generation;
    uint64_t monotonic_at_sync_us;
    int64_t  last_sync_wall_s;
    uint64_t sync_age_us;
    uint64_t max_sync_age_us;
    int16_t  utc_offset_minutes;
    uint8_t  offset_version;
    uint64_t last_eval_us;
} TimeService;

void TimeService_Init(TimeService *svc, uint64_t now_monotonic_us);

bool TimeService_SetWallTime(TimeService *svc, int64_t wall_time_s,
                              SystemTimeQuality quality,
                              uint8_t source,
                              uint64_t now_monotonic_us);

int64_t     TimeService_GetWallTime(const TimeService *svc);
bool        TimeService_IsTimeValid(const TimeService *svc);
SystemTimeQuality TimeService_GetQuality(const TimeService *svc);
uint32_t    TimeService_GetGeneration(const TimeService *svc);
uint16_t    TimeService_GetUtcOffset(const TimeService *svc);

void TimeService_Tick(TimeService *svc, uint64_t now_monotonic_us);

#endif
