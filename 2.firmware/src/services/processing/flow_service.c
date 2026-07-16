#include "services/processing/flow_service.h"
#include "infrastructure/numeric/checked_math.h"
#include "infrastructure/numeric/interpolation.h"
#include <string.h>

void flow_service_init(FlowService *svc, AppEventQueue *eq, DataRepository *repo)
{
    memset(svc, 0, sizeof(*svc));
    svc->event_queue = eq;
    svc->repo = repo;
    svc->generation = 1;
}

void flow_service_set_profile(FlowService *svc, const FlowProfile *p) { if (svc) svc->active_profile = p; }
void flow_service_set_calibration(FlowService *svc, const CalibrationRecord *c) { if (svc) svc->active_cal = c; }

FlowProcessStatus flow_compute(
    int64_t tof_up_ps, int64_t tof_down_ps,
    int32_t paired_temp_mdeg_c,
    const FlowProfile *profile,
    const CalibrationRecord *cal,
    FlowCandidate *candidate)
{
    if (!profile || !cal || !candidate) return FLOW_INTERNAL_ERROR;
    memset(candidate, 0, sizeof(*candidate));

    if (tof_up_ps < INT64_C(8000000) ||
        tof_down_ps < INT64_C(8000000) ||
        tof_up_ps > INT64_C(8000000000) ||
        tof_down_ps > INT64_C(8000000000) ||
        profile->path_length <= 0 || profile->pipe_area <= 0 ||
        profile->acoustic_velocity <= 0)
        return FLOW_INVALID_TOF;

    /* Delta TOF = downstream - upstream (positive = forward flow) */
    int64_t delta_tof;
    if (!checked_sub_i64(tof_down_ps, tof_up_ps, &delta_tof))
        return FLOW_NUMERIC_ERROR;

    /* Small-signal transit-time relation:
     * v_mm/s = c_mm/s^2 * delta_t_ps * 1000 /
     *          (2 * L_milli_mm * 1e12).
     * Temperature pairing is mandatory even when the qualified profile uses a
     * fixed acoustic velocity; a future profile may provide c(T). */
    if (paired_temp_mdeg_c < -50000 || paired_temp_mdeg_c > 150000)
        return FLOW_TEMPERATURE_MISSING;
    int64_t c_squared;
    if (!checked_mul_i64(profile->acoustic_velocity,
                         profile->acoustic_velocity, &c_squared))
        return FLOW_NUMERIC_ERROR;
    int64_t numerator;
    if (!checked_mul_i64(c_squared, delta_tof, &numerator) ||
        !checked_mul_i64(numerator, INT64_C(1000), &numerator))
        return FLOW_NUMERIC_ERROR;
    int64_t denominator;
    if (!checked_mul_i64(profile->path_length, INT64_C(2000000000000),
                         &denominator) || denominator == 0)
        return FLOW_NUMERIC_ERROR;
    int64_t velocity_mm_per_s = numerator / denominator;

    /* 1 mm^3 == 1 uL; pipe_area is milli-mm^2. */
    int64_t flow_raw;
    if (!checked_mul_i64(velocity_mm_per_s, profile->pipe_area, &flow_raw))
        return FLOW_NUMERIC_ERROR;
    flow_raw /= INT64_C(1000);

    /* Apply calibration */
    int64_t flow_cal;
    if (!apply_gain_offset(flow_raw, cal->gain, cal->offset, cal->shift, &flow_cal))
        return FLOW_NUMERIC_ERROR;

    candidate->flow_ul_per_s = flow_cal;
    candidate->direction = flow_cal > 0 ? FLOW_DIRECTION_FORWARD
        : (flow_cal < 0 ? FLOW_DIRECTION_REVERSE : FLOW_DIRECTION_NONE);

    return FLOW_OK;
}

FlowProcessStatus flow_service_accept_tof(
    FlowService *svc,
    int64_t tof_up_ps, int64_t tof_down_ps,
    RepoWriteTxn *txn,
    uint32_t correlation_id)
{
    if (!svc || !txn) return FLOW_INTERNAL_ERROR;
    if (!svc->active_profile || !svc->active_cal) return FLOW_PROFILE_ERROR;

    /* Read temperature from repository snapshot */
    RuntimeSnapshot snapshot;
    if (!data_repository_snapshot_copy(svc->repo, &snapshot))
        return FLOW_TEMPERATURE_MISSING;

    ResultMetadata metadata;
    memset(&metadata, 0, sizeof(metadata));
    metadata.purpose = MEAS_PURPOSE_PRODUCTION;
    metadata.origin = DATA_ORIGIN_LIVE_DEVICE;
    metadata.provenance = PROVENANCE_MEASURED;
    metadata.validity = DATA_VALID;
    metadata.freshness = DATA_FRESH;
    metadata.acceptance = DATA_ACCEPTED;
    FlowProcessStatus accepted = flow_service_accept_sample(
        svc, tof_up_ps, tof_down_ps, &metadata, &snapshot.temperature, txn);
    if (accepted != FLOW_OK) return accepted;

    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_FLOW_RESULT_READY;
    evt.priority = EVENT_PRIO_MEASUREMENT;
    evt.delivery = DELIVERY_EDGE;
    evt.correlation_id = correlation_id;
    app_event_queue_post(svc->event_queue, &evt);
    return FLOW_OK;
}

FlowProcessStatus flow_service_accept_sample(
    FlowService *svc,
    int64_t tof_up_ps,
    int64_t tof_down_ps,
    const ResultMetadata *metadata,
    const TemperatureResult *paired_temperature,
    RepoWriteTxn *txn)
{
    if (!svc || !txn || !metadata || !paired_temperature)
        return FLOW_INTERNAL_ERROR;
    if (!svc->active_profile || !svc->active_cal) return FLOW_PROFILE_ERROR;
    if (paired_temperature->meta.validity != DATA_VALID ||
        paired_temperature->meta.freshness != DATA_FRESH ||
        paired_temperature->meta.acceptance != DATA_ACCEPTED)
        return FLOW_TEMPERATURE_MISSING;
    uint64_t flow_time = metadata->sample_monotonic_us;
    uint64_t temp_time = paired_temperature->meta.sample_monotonic_us;
    uint64_t age = flow_time > temp_time ? flow_time - temp_time
                                         : temp_time - flow_time;
    if (age > UINT64_C(5000000)) return FLOW_TEMPERATURE_STALE;

    FlowCandidate candidate;
    FlowProcessStatus status = flow_compute(
        tof_up_ps, tof_down_ps, paired_temperature->temperature_mdeg_c,
        svc->active_profile, svc->active_cal, &candidate);

    if (status != FLOW_OK) { svc->rejected_stale_count++; return status; }

    FlowResult result;
    memset(&result, 0, sizeof(result));
    result.meta = *metadata;
    result.meta.config_version = svc->active_profile->id.schema_version;
    result.meta.calibration_version = svc->active_cal->record_version;
    result.meta.binding.profile_version = svc->active_profile->id.schema_version;
    result.flow_ul_per_s = candidate.flow_ul_per_s;
    result.direction = candidate.direction;
    result.paired_temperature_sequence =
        paired_temperature->meta.sample_sequence;

    if (!txn_write_flow(txn, &result)) {
        svc->rejected_stale_count++;
        return FLOW_INTERNAL_ERROR;
    }

    svc->accepted_count++;
    return FLOW_OK;
}
