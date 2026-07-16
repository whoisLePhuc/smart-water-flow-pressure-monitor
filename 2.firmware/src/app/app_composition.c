#include "app_composition.h"
#include "event/scheduler.h"
#include "platform/include/monotonic_clock_port.h"
#include <string.h>

#define POWER_SCHEDULER_JOB_ID ((SchedulerJobId)0x0706u)
#define POWER_SCHEDULER_OWNER_ID 0x00000007u
#define POWER_SCHEDULER_GENERATION 1u
#define MICROSECONDS_PER_SECOND 1000000u

static void power_sample_due_handler(const AppEvent *event, void *context)
{
    AppComposition *comp = (AppComposition *)context;
    if (!comp || !event)
        return;
    (void)power_facade_process_sample(
        &comp->power, event->monotonic_timestamp_us);
}

bool app_composition_init(AppComposition *comp,
                          const LoopBudgetConfig *budget,
                          const AdcPort *adc_port,
                          const PowerConfig *power_config)
{
    if (!comp || !adc_port)
        return false;

    memset(comp, 0, sizeof(*comp));

    AppEventQueueConfig qcfg = {
        .capacity = APP_EVENT_QUEUE_DEFAULT_CAPACITY,
        .reserved_critical = APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL,
        .reserved_measurement = APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT
    };
    app_event_queue_init(&comp->queue, &qcfg);

    data_repository_init(&comp->repo);
    system_fsm_init(&comp->fsm);
    scheduler_init();

    app_event_loop_init(&comp->loop, &comp->queue, &comp->fsm, &comp->repo, &comp->mediator, budget);

    if (!comp->loop.initialized)
        return false;

    if (measurement_facade_init(&comp->measurement, &comp->repo, &comp->queue) != PORT_OK
        || storage_facade_init(&comp->storage, &comp->repo) != PORT_OK
        || connectivity_facade_init(&comp->connectivity, &comp->repo, &comp->queue) != PORT_OK
        || power_facade_init(&comp->power, power_config, adc_port,
                             &comp->repo, &comp->queue) != PORT_OK)
        return false;

    if (event_mediator_register(&comp->mediator, EVT_POWER_SAMPLE_DUE,
                                power_sample_due_handler, comp)
        != EVENT_MEDIATOR_OK)
        return false;

    uint64_t period_us = (uint64_t)comp->power.config.sample_period_s
        * (uint64_t)MICROSECONDS_PER_SECOND;
    uint64_t now_us = monotonic_now_us();
    if (UINT64_MAX - now_us < period_us)
        return false;
    if (scheduler_schedule_periodic(
            POWER_SCHEDULER_JOB_ID,
            POWER_SCHEDULER_OWNER_ID,
            EVT_POWER_SAMPLE_DUE,
            now_us + period_us,
            period_us,
            POWER_SCHEDULER_GENERATION,
            MISS_POLICY_SKIP,
            EVENT_PRIO_BACKGROUND) != SCHEDULE_OK)
        return false;

    comp->initialized = true;
    return true;
}
