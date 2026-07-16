#ifndef SWFPM_MEASUREMENT_FACADE_H
#define SWFPM_MEASUREMENT_FACADE_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/measurement/measurement_types.h"
#include "port_status.h"

typedef struct {
    struct DataRepository *repo;
    struct AppEventQueue  *queue;
    bool initialized;
} MeasurementFacade;

PortStatus measurement_facade_init(MeasurementFacade *f, void *repo, void *queue);
void measurement_facade_process_sample(MeasurementFacade *f);
const FlowResult   *measurement_facade_get_flow(MeasurementFacade *f);
const PressureResult *measurement_facade_get_pressure(MeasurementFacade *f);

#endif
