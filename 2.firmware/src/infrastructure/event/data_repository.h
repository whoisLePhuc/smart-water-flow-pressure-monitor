#ifndef SWFPM_DATA_REPOSITORY_H
#define SWFPM_DATA_REPOSITORY_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "domain/common/metadata.h"
#include "infrastructure/event/event_id.h"
#include "infrastructure/repositories/runtime_snapshot.h"

/* =================================================================
 * Publication result
 * ================================================================= */

typedef enum {
    PUBLISH_OK,
    PUBLISH_REJECTED_STALE,
    PUBLISH_REJECTED_PROVENANCE,
    PUBLISH_REJECTED_INVALID,
    PUBLISH_BUFFER_BUSY
} DataPublishResult;

/* =================================================================
 * Source event token — prevents duplicate snapshot publication
 * for the same source event in one event-loop turn
 * ================================================================= */

typedef struct {
    EventId  source_event_id;
    uint64_t event_sequence;
    bool     snapshot_published_in_turn;
} SourceEventToken;

/* =================================================================
 * Snapshot read handle — enforce capture-once semantics
 * ================================================================= */

typedef struct {
    uint8_t              active_index;
    uint64_t             snapshot_version;
    const RuntimeSnapshot *snapshot;
    bool                 acquired;
} SnapshotReadHandle;

/* =================================================================
 * Repository — exposed struct (static allocation only)
 * ================================================================= */

typedef struct {
    RuntimeSnapshot              buffers[2];
    _Atomic uint_fast8_t         active_index;
    uint64_t                     snapshot_version;
} DataRepository;

/* =================================================================
 * API
 * ================================================================= */

void data_repository_init(DataRepository *repo);

DataPublishResult data_repository_accept_flow(
    DataRepository *repo,
    const FlowResult *result,
    SourceEventToken *token);

DataPublishResult data_repository_accept_pressure(
    DataRepository *repo,
    const PressureResult *result,
    SourceEventToken *token);

DataPublishResult data_repository_accept_temperature(
    DataRepository *repo,
    const TemperatureResult *result,
    SourceEventToken *token);

DataPublishResult data_repository_accept_volume(
    DataRepository *repo,
    const VolumeState *volume,
    SourceEventToken *token);

DataPublishResult data_repository_accept_leak(
    DataRepository *repo,
    const LeakDetectionResult *leak,
    SourceEventToken *token);

DataPublishResult data_repository_accept_mode(
    DataRepository *repo,
    const SystemModeContext *mode,
    SourceEventToken *token);

/* Snapshot access */
SnapshotReadHandle data_repository_snapshot_acquire(DataRepository *repo);

const RuntimeSnapshot *snapshot_read_ptr(const SnapshotReadHandle *handle);

void data_repository_snapshot_release(SnapshotReadHandle *handle);

/* Publish final snapshot for the current turn (called by event loop) */
bool data_repository_publish_if_requested(DataRepository *repo);

/* Initialize a source event token */
void data_repository_init_token(SourceEventToken *token, EventId source_event_id);

/* Production-eligibility guard — result accepted only when
 * purpose=PRODUCTION, origin=LIVE_DEVICE, provenance=MEASURED */
bool data_is_production(const ResultMetadata *meta);

#endif /* SWFPM_DATA_REPOSITORY_H */
