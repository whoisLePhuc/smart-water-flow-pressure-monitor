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
 * API
 * ================================================================= */

void data_repository_init(DataRepository *repo)
{
    if (!repo)
        return;

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
static DataPublishResult accept_result(
    DataRepository *repo,
    const void *result,
    size_t result_size,
    size_t offset_in_snapshot,
    SourceEventToken *token)
{
    if (!repo || !result || !token)
        return PUBLISH_REJECTED_INVALID;

    if (!repo->legacy_publish_pending) {
        if (repo->writer_active)
            return PUBLISH_BUFFER_BUSY;
        if (token->snapshot_published_in_turn)
            return PUBLISH_REJECTED_STALE;

        uint_fast8_t active = atomic_load_explicit(
            &repo->active_index, memory_order_acquire);
        repo->write_index = (uint8_t)(active ^ (uint_fast8_t)1);
        memcpy(&repo->buffers[repo->write_index],
               &repo->buffers[(uint8_t)active],
               sizeof(RuntimeSnapshot));
        repo->writer_active = true;
        repo->legacy_publish_pending = true;
        repo->legacy_source_token = token;

        /* Reserve this token for exactly one final publication.  Further
         * consequences in the same synchronous source-event turn are still
         * accepted because they carry the same token object. */
        token->snapshot_published_in_turn = true;
    } else if (repo->legacy_source_token != token) {
        return PUBLISH_BUFFER_BUSY;
    }

    uint8_t *base = (uint8_t *)&repo->buffers[repo->write_index];
    memcpy(base + offset_in_snapshot, result, result_size);
    return PUBLISH_OK;
}

#define SNAP_OFFSET(field) ((size_t)(uintptr_t)&((RuntimeSnapshot *)0)->field)

DataPublishResult data_repository_accept_flow(
    DataRepository *repo, const FlowResult *result, SourceEventToken *token)
{
    if (!result || !data_is_production(&result->meta))
        return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, result, sizeof(FlowResult), SNAP_OFFSET(flow), token);
}

DataPublishResult data_repository_accept_pressure(
    DataRepository *repo, const PressureResult *result, SourceEventToken *token)
{
    if (!result) return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, result, sizeof(PressureResult), SNAP_OFFSET(pressure), token);
}

DataPublishResult data_repository_accept_temperature(
    DataRepository *repo, const TemperatureResult *result, SourceEventToken *token)
{
    if (!result) return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, result, sizeof(TemperatureResult), SNAP_OFFSET(temperature), token);
}

DataPublishResult data_repository_accept_volume(
    DataRepository *repo, const VolumeState *volume, SourceEventToken *token)
{
    if (!volume) return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, volume, sizeof(VolumeState), SNAP_OFFSET(volume), token);
}

DataPublishResult data_repository_accept_leak(
    DataRepository *repo, const LeakDetectionResult *leak, SourceEventToken *token)
{
    if (!leak) return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, leak, sizeof(LeakDetectionResult), SNAP_OFFSET(leak), token);
}

DataPublishResult data_repository_accept_mode(
    DataRepository *repo, const SystemModeContext *mode, SourceEventToken *token)
{
    if (!mode) return PUBLISH_REJECTED_INVALID;
    return accept_result(repo, mode, sizeof(SystemModeContext), SNAP_OFFSET(mode), token);
}

/* =================================================================
 * Snapshot access
 * ================================================================= */

bool data_repository_snapshot_copy(DataRepository *repo,
                                   RuntimeSnapshot *snapshot_out)
{
    if (!repo || !snapshot_out)
        return false;

    for (uint8_t attempt = 0u; attempt < 3u; attempt++) {
        uint_fast8_t before = atomic_load_explicit(
            &repo->active_index, memory_order_acquire);
        uint64_t version = repo->buffers[(uint8_t)before].snapshot_version;
        memcpy(snapshot_out, &repo->buffers[(uint8_t)before],
               sizeof(*snapshot_out));
        uint_fast8_t after = atomic_load_explicit(
            &repo->active_index, memory_order_acquire);
        if (before == after && snapshot_out->snapshot_version == version)
            return true;
    }
    return false;
}

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
    if (!repo || !repo->legacy_publish_pending || !repo->writer_active)
        return false;

    repo->snapshot_version++;
    repo->buffers[repo->write_index].snapshot_version = repo->snapshot_version;
    atomic_store_explicit(&repo->active_index,
                          (uint_fast8_t)repo->write_index,
                          memory_order_release);

    repo->writer_active = false;
    repo->legacy_publish_pending = false;
    repo->legacy_source_token = NULL;
    return true;
}
