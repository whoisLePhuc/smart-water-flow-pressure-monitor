#ifndef SWFPM_SCHEDULER_H
#define SWFPM_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/common/metadata.h"
#include "infrastructure/event/event_id.h"
#include "infrastructure/queues/app_event_queue.h"   /* AppEvent, AppEventPriority */


typedef uint16_t SchedulerJobId;
#define SCHEDULER_INVALID_JOB_ID 0xFFFF

typedef enum {
    MISS_POLICY_SKIP,          /* Advance to the next anchor without catch-up. */
    MISS_POLICY_SIGNAL_ONCE,   /* Emit one late event, then advance the anchor. */
    MISS_POLICY_OWNER_SPECIFIC /* Owner decides recovery after the missed event. */
} MissPolicy;

typedef enum {
    SCHEDULE_OK,
    SCHEDULE_REJECTED_INVALID_PERIOD,
    SCHEDULE_REJECTED_DUPLICATE_ID,
    SCHEDULE_REJECTED_FULL,
    SCHEDULE_CANCELLED,
    SCHEDULE_NOT_FOUND
} ScheduleResult;

typedef enum {
    SCHEDULER_DUE_FIRED,
    SCHEDULER_DUE_NONE,
    SCHEDULER_DUE_MISSED
} SchedulerDueResult;

typedef struct {
    SchedulerJobId job_id;
    uint32_t       owner_id;
    EventId        event_id;
    uint64_t       deadline_us;       /* Absolute monotonic deadline */
    uint64_t       anchor_us;         /* Periodic anchor */
    uint64_t       period_us;         /* 0 = one-shot */
    uint32_t       generation;        /* Rejects events from a cancelled/replaced job. */
    uint32_t       mode_mask;
    MissPolicy     miss_policy;
    AppEventPriority priority;
    bool           pending;           /* Prevents duplicate due posts before acknowledge. */
} SchedulerJob;

#define SCHEDULER_MAX_JOBS 16

typedef struct {
    SchedulerJob jobs[SCHEDULER_MAX_JOBS];
    uint8_t job_count; /* Dense prefix; cancellation compacts this array. */
} Scheduler;


void scheduler_init(Scheduler *scheduler);

ScheduleResult scheduler_schedule_one_shot(
    Scheduler *scheduler,
    SchedulerJobId job_id,
    uint32_t owner_id,
    EventId event_id,
    uint64_t deadline_us,
    uint32_t generation,
    AppEventPriority priority);

ScheduleResult scheduler_schedule_periodic(
    Scheduler *scheduler,
    SchedulerJobId job_id,
    uint32_t owner_id,
    EventId event_id,
    uint64_t anchor_us,
    uint64_t period_us,
    uint32_t generation,
    MissPolicy miss_policy,
    AppEventPriority priority);

ScheduleResult scheduler_cancel(Scheduler *scheduler,
                                SchedulerJobId job_id,
                                uint32_t expected_generation);

// Clears a job's pending flag after its deadline event is consumed. Generation
// matching prevents a stale event from acknowledging a replacement job.
bool scheduler_acknowledge(Scheduler *scheduler,
                           SchedulerJobId job_id,
                           uint32_t expected_generation);

// Copies at most max_events due events into caller-owned storage. This does not
// post to AppEventQueue; the caller owns admission and failure handling.
uint8_t scheduler_dispatch_due(Scheduler *scheduler,
                               uint64_t now_us,
                               AppEvent *events_out,
                               uint8_t max_events);

// Returns false when there is no scheduled work. deadline_us is unchanged on
// failure.
bool scheduler_get_next_deadline(const Scheduler *scheduler,
                                 uint64_t *deadline_us);

#endif /* SWFPM_SCHEDULER_H */
