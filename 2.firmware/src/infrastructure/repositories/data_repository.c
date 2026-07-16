#include "infrastructure/repositories/data_repository.h"
#include <stdatomic.h>
#include <string.h>

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

bool data_repository_snapshot_copy(const DataRepository *repo,
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
