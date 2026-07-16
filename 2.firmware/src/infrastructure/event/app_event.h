#ifndef SWFPM_APP_EVENT_H
#define SWFPM_APP_EVENT_H

#include <stdbool.h>
#include "infrastructure/queues/app_event_queue.h"


/* Check if an event is stale based on source generation mismatch */
bool app_event_is_stale(const AppEvent *event, uint32_t current_generation);

/* Check if an event's correlation_id matches a given request ID */
bool app_event_match_correlation(const AppEvent *event, uint32_t correlation_id);

/* Build a completion event */
void app_event_make_completion(
    AppEvent *event_out,
    EventId id,
    uint32_t source_id,
    uint32_t correlation_id,
    uint32_t source_generation,
    bool success);

/* Build a deadline/due event */
void app_event_make_deadline(
    AppEvent *event_out,
    EventId id,
    uint32_t source_id,
    uint32_t job_id,
    uint32_t generation);

#endif /* SWFPM_APP_EVENT_H */
