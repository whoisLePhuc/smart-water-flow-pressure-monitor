#ifndef SWFPM_MEASUREMENT_MANAGER_H
#define SWFPM_MEASUREMENT_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "core/data_model.h"
#include "core/app_event_queue.h"
#include "core/data_repository.h"
#include "core/scheduler.h"
#include "drivers/max35103.h"
#include "drivers/zssc3241.h"

/* MeasurementManager — schedules and coordinates MAX35103 and ZSSC3241
 * measurement cycles. Routes events from the event router to the correct
 * driver instance. */

typedef struct {
    Max35103Driver  max;
    Zssc3241Driver  zssc;

    AppEventQueue     *event_queue;
    DataRepository    *repo;
    bool               production_enabled;

    uint32_t max_cycles;
    uint32_t zssc_cycles;
} MeasurementManager;

void measurement_manager_init(MeasurementManager *mgr,
                               AppEventQueue *event_queue,
                               DataRepository *repo);

/* Process an event — returns true if event was consumed */
bool measurement_manager_process_event(MeasurementManager *mgr,
                                        const AppEvent *event);

#endif
