#include "app_composition.h"
#include <string.h>

void app_composition_init(AppComposition *comp, const LoopBudgetConfig *budget)
{
    memset(comp, 0, sizeof(*comp));

    AppEventQueueConfig qcfg = {
        .capacity = APP_EVENT_QUEUE_DEFAULT_CAPACITY,
        .reserved_critical = APP_EVENT_QUEUE_DEFAULT_RESERVED_CRITICAL,
        .reserved_measurement = APP_EVENT_QUEUE_DEFAULT_RESERVED_MEASUREMENT
    };
    app_event_queue_init(&comp->queue, &qcfg);

    data_repository_init(&comp->repo);
    system_fsm_init(&comp->fsm);

    app_event_loop_init(&comp->loop, &comp->queue, &comp->fsm, &comp->repo, &comp->mediator, budget);

    measurement_facade_init(&comp->measurement, &comp->repo, &comp->queue);
    storage_facade_init(&comp->storage, &comp->repo);
    connectivity_facade_init(&comp->connectivity, &comp->repo, &comp->queue);
    power_facade_init(&comp->power, NULL);

    comp->initialized = true;
}
