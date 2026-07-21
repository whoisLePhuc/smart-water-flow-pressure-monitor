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
#include "infrastructure/bus/i2c_bus_manager.h"

typedef struct {
    /* Owned runtime infrastructure. Their addresses stay stable after init. */
    EventMediator       mediator;
    AppEventQueue       queue;
    Scheduler           scheduler;
    DataRepository      repo;
    SystemModeManager   fsm;
    AppEventLoop        loop;
    I2cBusManager       shared_i2c_bus;

    MeasurementFacade   measurement;
    MeasurementManager  measurement_manager;
    StorageFacade       storage;
    ConnectivityFacade  connectivity;
    PowerFacade         power;

    bool initialized; /* True only after every binding and periodic job succeeds. */
} AppComposition;

// Builds the complete runtime object graph without heap allocation. adc_port
// and the configuration it references are borrowed and must outlive comp.
// Failure leaves comp unusable and initialized == false.
bool app_composition_init(AppComposition *comp,
                          const LoopBudgetConfig *budget,
                          const AdcPort *adc_port,
                          const PowerConfig *power_config);

/* Binds the one physical I2C port shared by pressure and storage clients. */
bool app_composition_bind_i2c_port(AppComposition *comp,
                                   const I2cPort *i2c_port);

#endif
