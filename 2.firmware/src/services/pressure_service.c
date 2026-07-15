#include "services/pressure_service.h"
#include "infrastructure/numeric/checked_math.h"
#include "infrastructure/numeric/interpolation.h"
#include <string.h>

void pressure_service_init(PressureService *svc, AppEventQueue *eq, DataRepository *repo)
{
    memset(svc, 0, sizeof(*svc)); svc->event_queue = eq; svc->repo = repo; svc->generation = 1;
}
void pressure_service_set_profile(PressureService *svc, const PressureProfile *p) { if(svc) svc->active_profile = p; }
void pressure_service_set_calibration(PressureService *svc, const CalibrationRecord *c) { if(svc) svc->active_cal = c; }

PressureProcessStatus pressure_convert(uint32_t raw_u24, uint8_t status,
    const PressureProfile *profile, const CalibrationRecord *cal,
    PressureCandidate *candidate)
{
    if (!profile || !cal || !candidate) return PRESSURE_INTERNAL_ERROR;
    memset(candidate, 0, sizeof(*candidate));

    if (status != 0) return PRESSURE_STATUS_INVALID;
    if (raw_u24 > 0xFFFFFF) return PRESSURE_INVALID_RAW;

    /* Endpoint mapping: linear interpolation between (raw_lo, pa_lo) and (raw_hi, pa_hi) */
    if (profile->endpoint_hi_raw <= profile->endpoint_lo_raw)
        return PRESSURE_MAPPING_ERROR;

    int64_t raw_range = profile->endpoint_hi_raw - profile->endpoint_lo_raw;
    int64_t pa_range  = (int64_t)profile->endpoint_hi_pa - (int64_t)profile->endpoint_lo_pa;
    int64_t raw_offset = (int64_t)raw_u24 - profile->endpoint_lo_raw;

    int64_t pressure_raw;
    if (!checked_mul_i64(raw_offset, pa_range, &pressure_raw))
        return PRESSURE_NUMERIC_ERROR;
    pressure_raw = pressure_raw / raw_range + profile->endpoint_lo_pa;

    /* Apply field trim */
    int64_t pressure_cal;
    if (!apply_gain_offset(pressure_raw, cal->gain, cal->offset, cal->shift, &pressure_cal))
        return PRESSURE_NUMERIC_ERROR;

    candidate->pressure_pa = (int32_t)pressure_cal;
    return PRESSURE_OK;
}

PressureProcessStatus pressure_service_accept_raw(PressureService *svc,
    uint32_t raw_u24, uint8_t status, SourceEventToken *token)
{
    if (!svc || !token) return PRESSURE_INTERNAL_ERROR;
    if (!svc->active_profile || !svc->active_cal) return PRESSURE_PROFILE_ERROR;

    PressureCandidate candidate;
    PressureProcessStatus st = pressure_convert(raw_u24, status,
        svc->active_profile, svc->active_cal, &candidate);
    if (st != PRESSURE_OK) { svc->rejected_count++; return st; }

    PressureResult result;
    memset(&result, 0, sizeof(result));
    result.meta = candidate.meta;
    result.meta.acceptance = DATA_ACCEPTED;
    result.pressure_pa = candidate.pressure_pa;

    DataPublishResult pr = data_repository_accept_pressure(svc->repo, &result, token);
    if (pr != PUBLISH_OK) { svc->rejected_count++; return PRESSURE_INTERNAL_ERROR; }

    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_PRESSURE_RESULT_READY;
    evt.priority = EVENT_PRIO_MEASUREMENT;
    evt.delivery = DELIVERY_EDGE;
    app_event_queue_post(svc->event_queue, &evt);

    svc->accepted_count++;
    return PRESSURE_OK;
}
