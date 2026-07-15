#include "time_service.h"
#include <string.h>

#define DEFAULT_MAX_SYNC_AGE_US  (604800ULL * 1000000ULL)  /* 7 days in us */

void TimeService_Init(TimeService *svc, uint64_t now_monotonic_us)
{
    memset(svc, 0, sizeof(*svc));
    svc->valid = false;
    svc->quality = SYS_TIME_INVALID;
    svc->time_generation = 1;
    svc->max_sync_age_us = DEFAULT_MAX_SYNC_AGE_US;
    svc->utc_offset_minutes = 420;  /* VN UTC+07:00 */
    svc->offset_version = 1;
    svc->last_eval_us = now_monotonic_us;
}

bool TimeService_SetWallTime(TimeService *svc, int64_t wall_time_s,
                              SystemTimeQuality quality,
                              uint8_t source,
                              uint64_t now_monotonic_us)
{
    if (!svc) return false;

    /* Reject implausible values */
    if (wall_time_s < 0) return false;
    if (wall_time_s > 2000000000LL) return false;  /* ~2033 */

    svc->wall_time_s = wall_time_s;
    svc->quality = quality;
    svc->active_source = source;
    svc->time_generation++;
    svc->monotonic_at_sync_us = now_monotonic_us;
    svc->last_sync_wall_s = wall_time_s;
    svc->sync_age_us = 0;
    svc->valid = (quality >= SYS_TIME_RTC_HOLDOVER);
    svc->last_eval_us = now_monotonic_us;

    return true;
}

int64_t TimeService_GetWallTime(const TimeService *svc)
{
    return svc ? svc->wall_time_s : 0;
}

bool TimeService_IsTimeValid(const TimeService *svc)
{
    return svc ? svc->valid : false;
}

SystemTimeQuality TimeService_GetQuality(const TimeService *svc)
{
    return svc ? svc->quality : SYS_TIME_INVALID;
}

uint32_t TimeService_GetGeneration(const TimeService *svc)
{
    return svc ? svc->time_generation : 0;
}

uint16_t TimeService_GetUtcOffset(const TimeService *svc)
{
    return svc ? (uint16_t)svc->utc_offset_minutes : 0;
}

void TimeService_Tick(TimeService *svc, uint64_t now_monotonic_us)
{
    if (!svc || !svc->valid) return;

    svc->last_eval_us = now_monotonic_us;

    if (now_monotonic_us >= svc->monotonic_at_sync_us) {
        svc->sync_age_us = now_monotonic_us - svc->monotonic_at_sync_us;

        if (svc->sync_age_us >= svc->max_sync_age_us) {
            svc->valid = false;
            svc->quality = SYS_TIME_INVALID;
            svc->time_generation++;
        }
    }
}
