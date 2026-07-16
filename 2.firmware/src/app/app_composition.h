#ifndef SWFPM_APP_COMPOSITION_H
#define SWFPM_APP_COMPOSITION_H

#include "event/app_event_queue.h"
#include "event/data_repository.h"
#include "event/system_fsm.h"
#include "event/app_event_loop.h"
#include "event/event_mediator.h"
#include "facades/measurement_facade.h"
#include "facades/storage_facade.h"
#include "facades/connectivity_facade.h"
#include "facades/power_facade.h"

typedef struct {
    EventMediator       mediator;
    AppEventQueue       queue;
    DataRepository      repo;
    SystemModeManager   fsm;
    AppEventLoop        loop;

    MeasurementFacade   measurement;
    StorageFacade       storage;
    ConnectivityFacade  connectivity;
    PowerFacade         power;

    bool initialized;
} AppComposition;

void app_composition_init(AppComposition *comp, const LoopBudgetConfig *budget);

#endif
