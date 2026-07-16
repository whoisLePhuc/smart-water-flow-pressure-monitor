#ifndef SWFPM_APP_COMPOSITION_H
#define SWFPM_APP_COMPOSITION_H

#include "infrastructure/queues/app_event_queue.h"
#include "infrastructure/repositories/data_repository.h"
#include "app/system_fsm.h"
#include "event/app_event_loop.h"
#include "event/event_mediator.h"
#include "facades/measurement_facade.h"
#include "facades/storage_facade.h"
#include "facades/connectivity_facade.h"
#include "facades/power_facade.h"
#include "ports/adc_port.h"
#include "services/measurement/measurement_manager.h"

typedef struct {
    EventMediator       mediator;
    AppEventQueue       queue;
    Scheduler           scheduler;
    DataRepository      repo;
    SystemModeManager   fsm;
    AppEventLoop        loop;

    MeasurementFacade   measurement;
    MeasurementManager  measurement_manager;
    StorageFacade       storage;
    ConnectivityFacade  connectivity;
    PowerFacade         power;

    bool initialized;
} AppComposition;

bool app_composition_init(AppComposition *comp,
                          const LoopBudgetConfig *budget,
                          const AdcPort *adc_port,
                          const PowerConfig *power_config);

#endif
