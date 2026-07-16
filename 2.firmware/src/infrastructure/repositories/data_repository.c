#include "data_repository.h"
#include <stdatomic.h>
#include <string.h>

/* =================================================================
 * Production-eligibility guard
 * ================================================================= */

bool data_is_production(const ResultMetadata *meta)
{
    if (!meta)
        return false;
    return meta->purpose == MEAS_PURPOSE_PRODUCTION
        && meta->origin == DATA_ORIGIN_LIVE_DEVICE
        && meta->provenance == PROVENANCE_MEASURED;
}

/* =================================================================
 * Static default instance
 * ================================================================= */

static DataRepository default_repo;

/* =================================================================
 * API
 * ================================================================= */

void data_repository_init(DataRepository *repo)
{
    if (!repo)
        repo = &default_repo;

    memset(repo, 0, sizeof(*repo));
    atomic_store_explicit(&repo->active_index, 0, memory_order_release);

    /* Initialize schema version */
    repo->buffers[0].schema_version = 1;
    repo->buffers[0].snapshot_version = 0;
    repo->buffers[1].schema_version = 1;
    repo->buffers[1].snapshot_version = 0;
}

void data_repository_init_token(SourceEventToken *token, EventId source_event_id)
{
    if (!token) return;
    memset(token, 0, sizeof(*token));
    token->source_event_id = source_event_id;
}

/* @deprecated Use RepoWriteTxn for new code. These accept_* functions will be removed in Phase 11. */
static bool accept_result(
    DataRepository *repo,
    const void *result,
    size_t result_size,
    size_t offset_in_snapshot,
    SourceEventToken *token)
{
    if (!repo || !result || !token)
        return false;
    if (token->snapshot_published_in_turn)
        return false;
    if (!repo->accept_in_progress) {
        uint8_t active = atomic_load_explicit(&repo->active_index, memory_order_acquire);
        memcpy(&repo->inactive_buffer, &repo->buffers[active], sizeof(RuntimeSnapshot));
        repo->accept_in_progress = true;
        repo->current_token = *token;
    }
    uint8_t *base = (uint8_t *)&repo->inactive_buffer;
    memcpy(base + offset_in_snapshot, result, result_size);
    repo->publish_pending = true;
    return true;
}

#define SNAP_OFFSET(field) ((size_t)(uintptr_t)&((RuntimeSnapshot *)0)->field)

DataPublishResult data_repository_accept_flow(
    DataRepository *repo, const FlowResult *result, SourceEventToken *token)
{
    if (!result || !data_is_production(&result->meta))
        return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, result, sizeof(FlowResult), SNAP_OFFSET(flow), token)
           ? PUBLISH_OK : PUBLISH_REJECTED_STALE;
}

DataPublishResult data_repository_accept_pressure(
    DataRepository *repo, const PressureResult *result, SourceEventToken *token)
{
    if (!result) return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, result, sizeof(PressureResult), SNAP_OFFSET(pressure), token)
           ? PUBLISH_OK : PUBLISH_REJECTED_STALE;
}

DataPublishResult data_repository_accept_temperature(
    DataRepository *repo, const TemperatureResult *result, SourceEventToken *token)
{
    if (!result) return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, result, sizeof(TemperatureResult), SNAP_OFFSET(temperature), token)
           ? PUBLISH_OK : PUBLISH_REJECTED_STALE;
}

DataPublishResult data_repository_accept_volume(
    DataRepository *repo, const VolumeState *volume, SourceEventToken *token)
{
    if (!volume) return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, volume, sizeof(VolumeState), SNAP_OFFSET(volume), token)
           ? PUBLISH_OK : PUBLISH_REJECTED_STALE;
}

DataPublishResult data_repository_accept_leak(
    DataRepository *repo, const LeakDetectionResult *leak, SourceEventToken *token)
{
    if (!leak) return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, leak, sizeof(LeakDetectionResult), SNAP_OFFSET(leak), token)
           ? PUBLISH_OK : PUBLISH_REJECTED_STALE;
}

DataPublishResult data_repository_accept_mode(
    DataRepository *repo, const SystemModeContext *mode, SourceEventToken *token)
{
    if (!mode) return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, mode, sizeof(SystemModeContext), SNAP_OFFSET(mode), token)
           ? PUBLISH_OK : PUBLISH_REJECTED_STALE;
}

/* =================================================================
 * Snapshot access
 * ================================================================= */

SnapshotReadHandle data_repository_snapshot_acquire(DataRepository *repo)
{
    SnapshotReadHandle handle;
    memset(&handle, 0, sizeof(handle));

    if (!repo)
        return handle;

    uint8_t idx = atomic_load_explicit(&repo->active_index, memory_order_acquire);
    handle.active_index = idx;
    handle.snapshot_version = repo->buffers[idx].snapshot_version;
    handle.snapshot = &repo->buffers[idx];
    handle.acquired = true;
    return handle;
}

const RuntimeSnapshot *snapshot_read_ptr(const SnapshotReadHandle *handle)
{
    if (!handle || !handle->acquired)
        return NULL;
    return handle->snapshot;
}

void data_repository_snapshot_release(SnapshotReadHandle *handle)
{
    if (handle)
        handle->acquired = false;
}

/* =================================================================
 * Publication
 * ================================================================= */

bool data_repository_publish_if_requested(DataRepository *repo)
{
    if (!repo || !repo->publish_pending)
        return false;

    /* Finalize inactive buffer */
    repo->snapshot_version++;
    repo->inactive_buffer.snapshot_version = repo->snapshot_version;

    /* Copy completed inactive to the inactive buffer slot, then swap */
    uint8_t inactive_idx = atomic_load_explicit(&repo->active_index, memory_order_acquire) ^ 1U;
    memcpy(&repo->buffers[inactive_idx], &repo->inactive_buffer, sizeof(RuntimeSnapshot));

    atomic_store_explicit(&repo->active_index, inactive_idx, memory_order_release);

    /* Reset accept state */
    repo->accept_in_progress = false;
    repo->publish_pending = false;
    repo->current_token.snapshot_published_in_turn = true;

    return true;
}
