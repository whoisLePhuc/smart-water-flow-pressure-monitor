#ifndef SWFPM_SCHEDULER_H
#define SWFPM_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "data_model.h"
#include "app_event_queue.h"   /* AppEvent, AppEventPriority */

/* =================================================================
 * Scheduler types
 * ================================================================= */

typedef uint16_t SchedulerJobId;
#define SCHEDULER_INVALID_JOB_ID 0xFFFF

typedef enum {
    MISS_POLICY_SKIP,          /* Skip missed deadline, continue at next */
    MISS_POLICY_SIGNAL_ONCE,   /* Fire one late indication, then skip */
    MISS_POLICY_OWNER_SPECIFIC /* Delegate to owner */
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
    uint32_t       generation;        /* Invalidated on cancel/replace */
    uint32_t       mode_mask;
    MissPolicy     miss_policy;
    AppEventPriority priority;
    bool           pending;           /* Due event posted but not consumed */
} SchedulerJob;

#define SCHEDULER_MAX_JOBS 16

/* =================================================================
 * API
 * ================================================================= */

void scheduler_init(void);

ScheduleResult scheduler_schedule_one_shot(
    SchedulerJobId job_id,
    uint32_t owner_id,
    EventId event_id,
    uint64_t deadline_us,
    uint32_t generation,
    AppEventPriority priority);

ScheduleResult scheduler_schedule_periodic(
    SchedulerJobId job_id,
    uint32_t owner_id,
    EventId event_id,
    uint64_t anchor_us,
    uint64_t period_us,
    uint32_t generation,
    MissPolicy miss_policy,
    AppEventPriority priority);

ScheduleResult scheduler_cancel(SchedulerJobId job_id, uint32_t expected_generation);

/* Dispatch all due jobs. Returns number of events dispatched. */
uint8_t scheduler_dispatch_due(uint64_t now_us, AppEvent *events_out, uint8_t max_events);

/* Get earliest deadline, returns false if no jobs */
bool scheduler_get_next_deadline(uint64_t *deadline_us);

#endif /* SWFPM_SCHEDULER_H */
