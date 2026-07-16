#include "services/processing/pressure_service.h"
#include "infrastructure/numeric/checked_math.h"
#include "infrastructure/numeric/interpolation.h"
#include <string.h>

void pressure_service_init(PressureService *svc, AppEventQueue *eq)
{
    memset(svc, 0, sizeof(*svc)); svc->event_queue = eq; svc->generation = 1;
}
void pressure_service_set_profile(PressureService *svc, const PressureProfile *p) { if(svc) svc->active_profile = p; }
void pressure_service_set_calibration(PressureService *svc, const CalibrationRecord *c) { if(svc) svc->active_cal = c; }

PressureProcessStatus pressure_convert(uint32_t raw_u24, uint8_t status,
    const PressureProfile *profile, const CalibrationRecord *cal,
    PressureCandidate *candidate)
{
    if (!profile || !cal || !candidate) return PRESSURE_INTERNAL_ERROR;
    memset(candidate, 0, sizeof(*candidate));

    /* ZSSC3241 general status: bit 7 must be zero, bit 6 reports powered,
     * bit 5 busy, bits 4:3 mode, and bits 2:0 fault evidence. */
    bool powered = (status & 0x40u) != 0u;
    bool reserved_mode = (status & 0x18u) == 0x18u;
    if ((status & 0x80u) != 0u || !powered || reserved_mode ||
        (status & 0x27u) != 0u)
        return PRESSURE_STATUS_INVALID;
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
    uint32_t raw_u24, uint8_t status, RepoWriteTxn *txn,
    uint32_t correlation_id)
{
    if (!svc || !txn) return PRESSURE_INTERNAL_ERROR;
    if (!svc->active_profile || !svc->active_cal) return PRESSURE_PROFILE_ERROR;

    ResultMetadata metadata;
    memset(&metadata, 0, sizeof(metadata));
    metadata.purpose = MEAS_PURPOSE_PRODUCTION;
    metadata.origin = DATA_ORIGIN_LIVE_DEVICE;
    metadata.provenance = PROVENANCE_MEASURED;
    metadata.validity = DATA_VALID;
    metadata.freshness = DATA_FRESH;
    metadata.acceptance = DATA_ACCEPTED;
    PressureProcessStatus result = pressure_service_accept_sample(
        svc, raw_u24, status, &metadata, txn);
    if (result != PRESSURE_OK)
        return result;

    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_PRESSURE_RESULT_READY;
    evt.priority = EVENT_PRIO_MEASUREMENT;
    evt.delivery = DELIVERY_EDGE;
    evt.correlation_id = correlation_id;
    app_event_queue_post(svc->event_queue, &evt);
    return PRESSURE_OK;
}

PressureProcessStatus pressure_service_accept_sample(
    PressureService *svc,
    uint32_t raw_u24,
    uint8_t status,
    const ResultMetadata *metadata,
    RepoWriteTxn *txn)
{
    if (!svc || !txn || !metadata) return PRESSURE_INTERNAL_ERROR;
    if (!svc->active_profile || !svc->active_cal) return PRESSURE_PROFILE_ERROR;

    PressureCandidate candidate;
    PressureProcessStatus st = pressure_convert(raw_u24, status,
        svc->active_profile, svc->active_cal, &candidate);
    if (st != PRESSURE_OK) { svc->rejected_count++; return st; }

    PressureResult result;
    memset(&result, 0, sizeof(result));
    result.meta = *metadata;
    result.meta.config_version = svc->active_profile->id.schema_version;
    result.meta.calibration_version = svc->active_cal->record_version;
    result.meta.binding.profile_version = svc->active_profile->id.schema_version;
    result.pressure_pa = candidate.pressure_pa;

    if (!txn_write_pressure(txn, &result)) {
        svc->rejected_count++;
        return PRESSURE_INTERNAL_ERROR;
    }

    svc->accepted_count++;
    return PRESSURE_OK;
}
