#ifndef SWFPM_DATA_REPOSITORY_H
#define SWFPM_DATA_REPOSITORY_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "infrastructure/repositories/runtime_snapshot.h"


typedef struct {
    RuntimeSnapshot buffers[2];          /* Active read view plus inactive write view. */
    _Atomic uint_fast8_t active_index;    /* Published with release/acquire ordering. */
    uint64_t snapshot_version;            /* Increments once per successful commit. */
    uint8_t write_index;                  /* Valid only while writer_active is true. */
    bool writer_active;                   /* Single-writer guard owned by RepoWriteTxn. */
} DataRepository;


void data_repository_init(DataRepository *repo);

bool data_repository_snapshot_copy(const DataRepository *repo,
                                   RuntimeSnapshot *snapshot_out);

#endif /* SWFPM_DATA_REPOSITORY_H */
