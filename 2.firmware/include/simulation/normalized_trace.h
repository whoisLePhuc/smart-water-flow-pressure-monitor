#ifndef SWFPM_NORMALIZED_TRACE_H
#define SWFPM_NORMALIZED_TRACE_H

#include <stdint.h>
#include <stdbool.h>
#include "core/data_model.h"

/*
 * Normalized deterministic trace record.
 *
 * Each record captures one observable event during simulation.
 * Golden traces are byte-identical across runs with same input/seed.
 *
 * No pointer addresses, host paths, thread IDs, or realtime timestamps
 * are included — only deterministic simulation state.
 */

#define TRACE_MAX_RECORDS 1024
#define TRACE_MAX_EVENTS   64

typedef struct {
    uint64_t virtual_time_us;
    uint32_t turn_number;
    uint32_t event_id;
    uint8_t  delivery_class;
    uint32_t correlation_id;
    uint32_t source_generation;
    uint32_t mode_generation;
    uint32_t resource_generation;
    uint8_t  event_owner;      /* EventOwner enum value */
    uint8_t  dispatch_result;  /* FsmDispatchResult or owner-specific */
    uint8_t  system_mode;      /* SystemMode enum value */
    uint32_t transition_sequence;
    uint32_t purpose;          /* MeasurementPurpose */
    uint32_t origin;           /* DataOrigin */
    uint32_t provenance;       /* DataProvenance */
    uint32_t diagnostic_flags;
} TraceRecord;

typedef struct {
    TraceRecord records[TRACE_MAX_RECORDS];
    uint16_t    count;
    uint32_t    schema_version;
} NormalizedTrace;

void trace_init(NormalizedTrace *trace);

bool trace_append(NormalizedTrace *trace, const TraceRecord *record);

uint16_t trace_get_count(const NormalizedTrace *trace);

const TraceRecord* trace_get(const NormalizedTrace *trace, uint16_t index);

/* Compare two traces for byte-equivalent determinism. */
bool trace_equals(const NormalizedTrace *a, const NormalizedTrace *b);

#endif
