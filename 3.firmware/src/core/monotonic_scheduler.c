#include "core/scheduler.h"
#include "platform/monotonic_clock_port.h"
#include <string.h>

/* =================================================================
 * Internal state
 * ================================================================= */

static SchedulerJob job_table[SCHEDULER_MAX_JOBS];
static uint8_t      job_count;

/* =================================================================
 * Helpers
 * ================================================================= */

static SchedulerJob *find_job(SchedulerJobId id)
{
    for (uint8_t i = 0; i < job_count; i++) {
        if (job_table[i].job_id == id)
            return &job_table[i];
    }
    return NULL;
}

static void remove_job_at(uint8_t idx)
{
    if (idx < job_count) {
        memmove(&job_table[idx], &job_table[idx + 1],
                (job_count - idx - 1) * sizeof(SchedulerJob));
        job_count--;
    }
}

/* Wrap-safe comparison for deadlines */
static bool is_deadline_passed(uint64_t deadline, uint64_t now)
{
    return deadline <= now;
}

/* Compute next anchored deadline */
static uint64_t next_anchored(uint64_t anchor, uint64_t period, uint64_t now)
{
    if (period == 0)
        return anchor;
    if (anchor > now)
        return anchor;
    uint64_t elapsed = now - anchor;
    uint64_t periods = elapsed / period;
    /* If we're past multiple periods, return the first future deadline */
    uint64_t next = anchor + (periods + 1) * period;
    /* Handle wrap: if next < anchor, we wrapped 64-bit — clamp to now */
    if (next < anchor)
        return now;
    return next;
}

/* =================================================================
 * API
 * ================================================================= */

void scheduler_init(void)
{
    memset(job_table, 0, sizeof(job_table));
    job_count = 0;
}

ScheduleResult scheduler_schedule_one_shot(
    SchedulerJobId job_id,
    uint32_t owner_id,
    EventId event_id,
    uint64_t deadline_us,
    uint32_t generation,
    AppEventPriority priority)
{
    if (job_count >= SCHEDULER_MAX_JOBS)
        return SCHEDULE_REJECTED_FULL;

    if (find_job(job_id) != NULL)
        return SCHEDULE_REJECTED_DUPLICATE_ID;

    SchedulerJob *job = &job_table[job_count];
    job->job_id     = job_id;
    job->owner_id   = owner_id;
    job->event_id   = event_id;
    job->deadline_us = deadline_us;
    job->anchor_us  = deadline_us;
    job->period_us  = 0;
    job->generation = generation;
    job->mode_mask  = 0xFFFFFFFF;
    job->miss_policy = MISS_POLICY_SKIP;
    job->priority   = priority;
    job->pending    = false;
    job_count++;

    return SCHEDULE_OK;
}

ScheduleResult scheduler_schedule_periodic(
    SchedulerJobId job_id,
    uint32_t owner_id,
    EventId event_id,
    uint64_t anchor_us,
    uint64_t period_us,
    uint32_t generation,
    MissPolicy miss_policy,
    AppEventPriority priority)
{
    if (period_us == 0)
        return SCHEDULE_REJECTED_INVALID_PERIOD;

    if (job_count >= SCHEDULER_MAX_JOBS)
        return SCHEDULE_REJECTED_FULL;

    if (find_job(job_id) != NULL)
        return SCHEDULE_REJECTED_DUPLICATE_ID;

    SchedulerJob *job = &job_table[job_count];
    job->job_id     = job_id;
    job->owner_id   = owner_id;
    job->event_id   = event_id;
    job->anchor_us  = anchor_us;
    job->period_us  = period_us;
    job->generation = generation;
    job->mode_mask  = 0xFFFFFFFF;
    job->miss_policy = miss_policy;
    job->priority   = priority;
    job->pending    = false;

    /* Set initial deadline */
    job->deadline_us = next_anchored(anchor_us, period_us, monotonic_now_us());
    job_count++;

    return SCHEDULE_OK;
}

ScheduleResult scheduler_cancel(SchedulerJobId job_id, uint32_t expected_generation)
{
    for (uint8_t i = 0; i < job_count; i++) {
        if (job_table[i].job_id == job_id) {
            if (job_table[i].generation != expected_generation)
                return SCHEDULE_NOT_FOUND;
            remove_job_at(i);
            return SCHEDULE_CANCELLED;
        }
    }
    return SCHEDULE_NOT_FOUND;
}

uint8_t scheduler_dispatch_due(uint64_t now_us, AppEvent *events_out, uint8_t max_events)
{
    uint8_t dispatched = 0;

    for (uint8_t i = 0; i < job_count && dispatched < max_events; ) {
        SchedulerJob *job = &job_table[i];

        if (!is_deadline_passed(job->deadline_us, now_us)) {
            i++;
            continue;
        }

        /* Handle missed deadlines */
        if (job->pending) {
            /* Already posted and not consumed — check miss policy */
            if (job->miss_policy == MISS_POLICY_SKIP) {
                /* Skip this deadline entirely */
                if (job->period_us > 0) {
                    job->deadline_us = next_anchored(job->anchor_us, job->period_us, now_us);
                    job->pending = false;
                } else {
                    remove_job_at(i);
                    continue;
                }
            } else {
                /* SIGNAL_ONCE or OWNER_SPECIFIC: keep pending, don't re-post */
                i++;
                continue;
            }
        }

        /* Build due event */
        AppEvent *evt = &events_out[dispatched];
        memset(evt, 0, sizeof(AppEvent));
        evt->id = job->event_id;
        evt->source_id = job->owner_id;
        evt->priority = job->priority;
        evt->delivery = DELIVERY_DEADLINE;
        evt->correlation_id = job->job_id;
        evt->source_generation = job->generation;
        evt->monotonic_timestamp_us = monotonic_now_us();
        dispatched++;

        /* Mark pending so we don't re-post if not consumed this turn */
        job->pending = true;

        /* Reschedule periodic */
        if (job->period_us > 0) {
            job->deadline_us = next_anchored(job->anchor_us, job->period_us, now_us);
        }

        i++;
    }

    return dispatched;
}

bool scheduler_get_next_deadline(uint64_t *deadline_us)
{
    if (job_count == 0 || !deadline_us)
        return false;

    uint64_t earliest = job_table[0].deadline_us;
    for (uint8_t i = 1; i < job_count; i++) {
        if (job_table[i].deadline_us < earliest)
            earliest = job_table[i].deadline_us;
    }
    *deadline_us = earliest;
    return true;
}
