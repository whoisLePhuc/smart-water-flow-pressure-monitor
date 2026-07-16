#include "power_facade.h"
#include <string.h>

PortStatus power_facade_init(PowerFacade *f, const PowerConfig *cfg)
{
    if (!f) return PORT_STATUS_INVALID_PARAM;
    memset(f, 0, sizeof(*f));
    if (cfg) f->config = *cfg;
    else f->config = (PowerConfig)POWER_CONFIG_DEFAULT;
    power_service_init(&f->svc, &f->config);
    f->initialized = true;
    return PORT_OK;
}

PortStatus power_facade_sample(PowerFacade *f, uint16_t raw_adc)
{
    if (!f || !f->initialized) return PORT_STATUS_UNAVAILABLE;
    return power_service_sample(&f->svc, raw_adc);
}

PortStatus power_facade_get_status(const PowerFacade *f)
{
    if (!f || !f->initialized) return PORT_STATUS_UNAVAILABLE;
    return PORT_OK;
}

PowerHealth power_facade_get_health(const PowerFacade *f)
{
    if (!f || !f->initialized) return POWER_STATE_UNKNOWN;
    return power_service_get_health(&f->svc);
}

uint16_t power_facade_get_mv(const PowerFacade *f)
{
    if (!f || !f->initialized) return 0xFFFF;
    return power_service_get_mv(&f->svc);
}
