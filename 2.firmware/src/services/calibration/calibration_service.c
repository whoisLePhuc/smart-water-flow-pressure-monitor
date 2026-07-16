#include "services/calibration/calibration_service.h"
#include "infrastructure/numeric/checked_math.h"
#include "infrastructure/numeric/interpolation.h"
#include <string.h>

void calibration_service_init(CalibrationService *svc,
                               AppEventQueue *event_queue)
{
    memset(svc, 0, sizeof(*svc));
    svc->event_queue = event_queue;
    svc->generation = 1;
}

void calibration_service_set_profile(CalibrationService *svc,
                                      const TemperatureProfile *profile)
{
    if (svc) svc->active_profile = profile;
}

void calibration_service_set_calibration(CalibrationService *svc,
                                          const CalibrationRecord *cal)
{
    if (svc) svc->active_cal = cal;
}

/* ── Pure conversion pipeline ─────────────────────────────── */

TemperatureProcessStatus temperature_convert_raw(
    uint16_t probe_integer, uint16_t probe_fraction,
    uint16_t ref_integer,   uint16_t ref_fraction,
    const TemperatureProfile *profile,
    const CalibrationRecord *cal,
    TemperatureCandidate *candidate)
{
    if (!profile || !cal || !candidate)
        return TEMP_INTERNAL_ERROR;

    memset(candidate, 0, sizeof(*candidate));

    /* 1. Q16 join: N = I * 2^16 + F */
    uint32_t n_probe = ((uint32_t)probe_integer << 16) | (uint32_t)probe_fraction;
    uint32_t n_ref   = ((uint32_t)ref_integer   << 16) | (uint32_t)ref_fraction;

    if (n_ref == 0)
        return TEMP_INVALID_SAMPLE;

    /* 2. Ratiometric resistance: R = round(N_probe * R_ref / N_ref) */
    /* Use widened multiplication for intermediate */
    uint64_t product = (uint64_t)n_probe * (uint64_t)profile->rtd_r0;
    int64_t resistance_uohm = (int64_t)(product / n_ref);

    /* 3. Reference/path correction: R_path = round(R * G_R) + O_R */
    int64_t corrected_res;
    if (!apply_gain_offset(resistance_uohm, cal->gain, cal->offset,
                           cal->shift, &corrected_res))
        return TEMP_NUMERIC_ERROR;

    candidate->resistance_uohm = corrected_res;

    /* 4. RTD table interpolation: R → T */
    {
        int64_t temp_raw = 0;
        if (!interpolate_i64(corrected_res,
                             profile->rtd_res_table, profile->rtd_temp_table,
                             profile->rtd_table_size, &temp_raw))
            return TEMP_NUMERIC_ERROR;
        candidate->unfiltered_temperature_mdeg_c = (int32_t)temp_raw;
    }

    candidate->processing_flags = 0;
    return TEMP_OK;
}

/* ── Stateful accept + publish ────────────────────────────── */

TemperatureProcessStatus calibration_service_accept_raw(
    CalibrationService *svc,
    uint16_t probe_integer, uint16_t probe_fraction,
    uint16_t ref_integer,   uint16_t ref_fraction,
    RepoWriteTxn *txn,
    uint32_t correlation_id)
{
    if (!svc || !txn)
        return TEMP_INTERNAL_ERROR;

    /* Check active profile */
    if (!svc->active_profile || !svc->active_cal)
        return TEMP_PROFILE_ERROR;

    /* Pure conversion */
    TemperatureCandidate candidate;
    TemperatureProcessStatus status = temperature_convert_raw(
        probe_integer, probe_fraction,
        ref_integer, ref_fraction,
        svc->active_profile, svc->active_cal, &candidate);

    if (status != TEMP_OK) {
        svc->rejected_invalid_count++;
        return status;
    }

    /* Build immutable TemperatureResult */
    TemperatureResult result;
    memset(&result, 0, sizeof(result));
    result.meta = candidate.meta;
    result.meta.purpose = MEAS_PURPOSE_PRODUCTION;
    result.meta.origin = DATA_ORIGIN_LIVE_DEVICE;
    result.meta.provenance = PROVENANCE_MEASURED;
    result.meta.validity = DATA_VALID;
    result.meta.freshness = DATA_FRESH;
    result.meta.acceptance = DATA_ACCEPTED;
    result.temperature_mdeg_c = candidate.unfiltered_temperature_mdeg_c;

    /* Publish to repository */
    if (!txn_write_temperature(txn, &result)) {
        svc->rejected_stale_count++;
        return TEMP_STALE_SAMPLE;
    }

    /* Post EVT_TEMPERATURE_RESULT_READY */
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_TEMPERATURE_RESULT_READY;
    evt.priority = EVENT_PRIO_MEASUREMENT;
    evt.delivery = DELIVERY_EDGE;
    evt.correlation_id = correlation_id;
    app_event_queue_post(svc->event_queue, &evt);

    svc->accepted_count++;
    return TEMP_OK;
}
