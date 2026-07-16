#include "services/flow_service.h"
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
    (void)paired_temp_mdeg_c;

    memset(candidate, 0, sizeof(*candidate));

    /* Delta TOF = downstream - upstream (positive = forward flow) */
    int64_t delta_tof;
    if (!checked_sub_i64(tof_down_ps, tof_up_ps, &delta_tof))
        return FLOW_NUMERIC_ERROR;

    /* Velocity = (path_length * delta_tof) / (2 * acoustic_velocity^2 * tof_up * tof_down)
     * Simplified for Phase 9: v = delta_tof * K where K = profile constant */
    int64_t velocity; /* pm/µs or equivalent */
    if (!checked_mul_i64(delta_tof, profile->path_length, &velocity))
        return FLOW_NUMERIC_ERROR;

    /* Volumetric flow = velocity * pipe_area */
    int64_t flow_raw;
    if (!checked_mul_i64(velocity, profile->pipe_area, &flow_raw))
        return FLOW_NUMERIC_ERROR;

    /* Apply calibration */
    int64_t flow_cal;
    if (!apply_gain_offset(flow_raw, cal->gain, cal->offset, cal->shift, &flow_cal))
        return FLOW_NUMERIC_ERROR;

    candidate->flow_ul_per_s = flow_cal;
    candidate->direction = (flow_cal >= 0) ? FLOW_DIRECTION_FORWARD : FLOW_DIRECTION_REVERSE;

    return FLOW_OK;
}

FlowProcessStatus flow_service_accept_tof(
    FlowService *svc,
    int64_t tof_up_ps, int64_t tof_down_ps,
    SourceEventToken *token)
{
    if (!svc || !token) return FLOW_INTERNAL_ERROR;
    if (!svc->active_profile || !svc->active_cal) return FLOW_PROFILE_ERROR;

    /* Read temperature from repository snapshot */
    RuntimeSnapshot snapshot;
    int32_t temp = data_repository_snapshot_copy(svc->repo, &snapshot)
        ? snapshot.temperature.temperature_mdeg_c : 0;

    FlowCandidate candidate;
    FlowProcessStatus status = flow_compute(
        tof_up_ps, tof_down_ps, temp,
        svc->active_profile, svc->active_cal, &candidate);

    if (status != FLOW_OK) { svc->rejected_stale_count++; return status; }

    FlowResult result;
    memset(&result, 0, sizeof(result));
    result.meta = candidate.meta;
    result.meta.acceptance = DATA_ACCEPTED;
    result.flow_ul_per_s = candidate.flow_ul_per_s;
    result.direction = candidate.direction;

    DataPublishResult pr = data_repository_accept_flow(svc->repo, &result, token);
    if (pr != PUBLISH_OK) { svc->rejected_stale_count++; return FLOW_INTERNAL_ERROR; }

    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = EVT_FLOW_RESULT_READY;
    evt.priority = EVENT_PRIO_MEASUREMENT;
    evt.delivery = DELIVERY_EDGE;
    evt.correlation_id = token->source_event_id;
    app_event_queue_post(svc->event_queue, &evt);

    svc->accepted_count++;
    return FLOW_OK;
}
