#include "measurement_facade.h"
#include <stddef.h>

PortStatus measurement_facade_init(MeasurementFacade *f, void *repo, void *queue)
{
    if (!f) return PORT_STATUS_INVALID_PARAM;
    f->repo = (struct DataRepository *)repo;
    f->queue = (struct AppEventQueue  *)queue;
    f->initialized = true;
    return PORT_OK;
}

void measurement_facade_process_sample(MeasurementFacade *f)
{
    (void)f;
}

const FlowResult *measurement_facade_get_flow(MeasurementFacade *f)
{
    (void)f;
    return NULL;
}

const PressureResult *measurement_facade_get_pressure(MeasurementFacade *f)
{
    (void)f;
    return NULL;
}
