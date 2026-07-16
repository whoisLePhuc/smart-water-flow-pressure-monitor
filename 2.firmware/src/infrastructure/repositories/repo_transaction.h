#ifndef SWFPM_REPO_TRANSACTION_H
#define SWFPM_REPO_TRANSACTION_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/measurement/measurement_types.h"
#include "domain/product/volume_types.h"
#include "domain/product/leak_types.h"
#include "domain/system/system_types.h"
#include "infrastructure/event/event_id.h"
#include "infrastructure/repositories/runtime_snapshot.h"
#include "domain/power/power_types.h"
#include "infrastructure/repositories/data_repository.h"

#define TXN_STATE_INIT       0
#define TXN_STATE_ACTIVE     1
#define TXN_STATE_COMMITTED  2
#define TXN_STATE_ABORTED    3
#define TXN_STATE_ERROR      4

typedef struct {
    DataRepository *repo;       /* Borrowed while active; NULL outside a transaction. */
    uint64_t base_generation;   /* Snapshot version observed by txn_begin(). */
    uint32_t written_fields_mask; /* Prevents ambiguous duplicate field writes. */
    uint8_t inactive_index;     /* Repository buffer reserved by this writer. */
    uint8_t state;              /* Enforces one terminal commit or abort. */
} RepoWriteTxn;

void txn_init(RepoWriteTxn *txn);

// Reserves the repository's inactive buffer for a single writer and seeds it
// from the active snapshot. A successful begin must end in commit or abort.
bool txn_begin(RepoWriteTxn *txn, DataRepository *repo);

// Atomically publishes the inactive buffer and advances snapshot_version.
// Commit is terminal; callers must not reuse txn without txn_init().
bool txn_commit(RepoWriteTxn *txn);
void txn_abort(RepoWriteTxn *txn);

bool txn_write_flow(RepoWriteTxn *txn, const FlowResult *result);
bool txn_write_pressure(RepoWriteTxn *txn, const PressureResult *result);
bool txn_write_temperature(RepoWriteTxn *txn, const TemperatureResult *result);
bool txn_write_volume(RepoWriteTxn *txn, const VolumeState *state);
bool txn_write_leak(RepoWriteTxn *txn, const LeakDetectionResult *leak);
bool txn_write_mode(RepoWriteTxn *txn, const SystemModeContext *mode);
bool txn_write_power(RepoWriteTxn *txn, const PowerSnapshot *power);

// Copies the transaction's in-progress snapshot. This is the only supported
// way for later services in the same dispatch to observe earlier writes.
bool txn_read_snapshot(const RepoWriteTxn *txn, RuntimeSnapshot *snapshot_out);

#endif /* SWFPM_REPO_TRANSACTION_H */
