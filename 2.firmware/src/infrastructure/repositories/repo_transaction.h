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
#include "data_repository.h"

/* ── Transaction states ── */

#define TXN_STATE_INIT       0
#define TXN_STATE_ACTIVE     1
#define TXN_STATE_COMMITTED  2
#define TXN_STATE_ABORTED    3
#define TXN_STATE_ERROR      4

/* ── Transaction type ── */

typedef struct {
    DataRepository       *repo;
    uint64_t               base_generation;
    uint32_t               written_fields_mask;
    uint8_t                inactive_index;
    uint8_t                state;
} RepoWriteTxn;

/* ── API: lifecycle ── */

void txn_init(RepoWriteTxn *txn);
bool txn_begin(RepoWriteTxn *txn, DataRepository *repo);
bool txn_commit(RepoWriteTxn *txn);
void txn_abort(RepoWriteTxn *txn);

/* ── API: typed writes ── */

bool txn_write_flow(RepoWriteTxn *txn, const FlowResult *result);
bool txn_write_pressure(RepoWriteTxn *txn, const PressureResult *result);
bool txn_write_temperature(RepoWriteTxn *txn, const TemperatureResult *result);
bool txn_write_volume(RepoWriteTxn *txn, const VolumeState *state);
bool txn_write_leak(RepoWriteTxn *txn, const LeakDetectionResult *leak);
bool txn_write_mode(RepoWriteTxn *txn, const SystemModeContext *mode);
bool txn_write_power(RepoWriteTxn *txn, const PowerSnapshot *power);

/* ── API: read ── */

bool txn_read_snapshot(const RepoWriteTxn *txn, RuntimeSnapshot *snapshot_out);

#endif /* SWFPM_REPO_TRANSACTION_H */
