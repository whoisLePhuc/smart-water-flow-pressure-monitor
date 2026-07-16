#include "power_facade.h"

PortStatus power_facade_init(PowerFacade *f, void *adc)
{
    if (!f) return PORT_STATUS_INVALID_PARAM;
    f->adc = (struct AdcPort *)adc;
    f->initialized = true;
    return PORT_OK;
}

PortStatus power_facade_get_status(PowerFacade *f)
{
    if (!f || !f->initialized) return PORT_STATUS_UNAVAILABLE;
    return PORT_OK;
}

PowerHealth power_facade_get_health(PowerFacade *f)
{
    (void)f;
    return POWER_STATE_UNKNOWN;
}
