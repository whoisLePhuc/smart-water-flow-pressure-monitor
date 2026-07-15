#include "normalized_trace.h"
#include <string.h>

void trace_init(NormalizedTrace *trace)
{
    if (!trace) return;
    memset(trace, 0, sizeof(*trace));
    trace->schema_version = 1;
}

bool trace_append(NormalizedTrace *trace, const TraceRecord *record)
{
    if (!trace || !record) return false;
    if (trace->count >= TRACE_MAX_RECORDS) return false;

    trace->records[trace->count] = *record;
    trace->count++;
    return true;
}

uint16_t trace_get_count(const NormalizedTrace *trace)
{
    return trace ? trace->count : 0;
}

const TraceRecord* trace_get(const NormalizedTrace *trace, uint16_t index)
{
    if (!trace || index >= trace->count) return NULL;
    return &trace->records[index];
}

bool trace_equals(const NormalizedTrace *a, const NormalizedTrace *b)
{
    if (!a || !b) return false;
    if (a->count != b->count) return false;

    for (uint16_t i = 0; i < a->count; i++) {
        const TraceRecord *ra = &a->records[i];
        const TraceRecord *rb = &b->records[i];

        if (ra->virtual_time_us      != rb->virtual_time_us)      return false;
        if (ra->turn_number          != rb->turn_number)          return false;
        if (ra->event_id             != rb->event_id)             return false;
        if (ra->correlation_id       != rb->correlation_id)       return false;
        if (ra->source_generation    != rb->source_generation)    return false;
        if (ra->mode_generation      != rb->mode_generation)      return false;
        if (ra->resource_generation  != rb->resource_generation)  return false;
        if (ra->event_owner          != rb->event_owner)          return false;
        if (ra->system_mode          != rb->system_mode)          return false;
        if (ra->transition_sequence  != rb->transition_sequence)  return false;
        if (ra->purpose              != rb->purpose)              return false;
        if (ra->origin               != rb->origin)               return false;
        if (ra->provenance           != rb->provenance)           return false;
    }
    return true;
}
