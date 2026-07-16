#ifndef SWFPM_DATA_REPOSITORY_H
#define SWFPM_DATA_REPOSITORY_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "infrastructure/repositories/runtime_snapshot.h"

/* =================================================================
 * Repository — exposed struct (static allocation only)
 * ================================================================= */

typedef struct {
    RuntimeSnapshot              buffers[2];
    _Atomic uint_fast8_t         active_index;
    uint64_t                     snapshot_version;
    uint8_t                      write_index;
    bool                         writer_active;
} DataRepository;

/* =================================================================
 * API
 * ================================================================= */

void data_repository_init(DataRepository *repo);

bool data_repository_snapshot_copy(const DataRepository *repo,
                                   RuntimeSnapshot *snapshot_out);

#endif /* SWFPM_DATA_REPOSITORY_H */
