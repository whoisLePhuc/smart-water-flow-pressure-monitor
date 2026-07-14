#include "core/app_event.h"
#include <string.h>

bool app_event_is_stale(const AppEvent *event, uint32_t current_generation)
{
    if (!event)
        return true;
    return event->source_generation != current_generation;
}

bool app_event_match_correlation(const AppEvent *event, uint32_t correlation_id)
{
    if (!event)
        return false;
    return event->correlation_id == correlation_id;
}

void app_event_make_completion(
    AppEvent *event_out,
    EventId id,
    uint32_t source_id,
    uint32_t correlation_id,
    uint32_t source_generation,
    bool success)
{
    if (!event_out)
        return;

    memset(event_out, 0, sizeof(*event_out));
    event_out->id = id;
    event_out->source_id = source_id;
    event_out->priority = EVENT_PRIO_MEASUREMENT;
    event_out->delivery = DELIVERY_COMPLETION;
    event_out->correlation_id = correlation_id;
    event_out->source_generation = source_generation;
    event_out->payload[0] = success ? 1 : 0;
    event_out->payload_size = 1;
}

void app_event_make_deadline(
    AppEvent *event_out,
    EventId id,
    uint32_t source_id,
    uint32_t job_id,
    uint32_t generation)
{
    if (!event_out)
        return;

    memset(event_out, 0, sizeof(*event_out));
    event_out->id = id;
    event_out->source_id = source_id;
    event_out->priority = EVENT_PRIO_MEASUREMENT;
    event_out->delivery = DELIVERY_DEADLINE;
    event_out->correlation_id = job_id;
    event_out->source_generation = generation;
}
