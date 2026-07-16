#ifndef SWFPM_POWER_FACADE_H
#define SWFPM_POWER_FACADE_H

#include <stdint.h>
#include "port_status.h"
#include "domain/power/power_types.h"
#include "domain/power/power_config.h"
#include "services/power/power_service.h"

typedef struct {
    PowerService svc;
    PowerConfig  config;
    bool         initialized;
} PowerFacade;

PortStatus power_facade_init(PowerFacade *f, const PowerConfig *cfg);
PortStatus power_facade_sample(PowerFacade *f, uint16_t raw_adc);
PortStatus power_facade_get_status(const PowerFacade *f);
PowerHealth power_facade_get_health(const PowerFacade *f);
uint16_t  power_facade_get_mv(const PowerFacade *f);

#endif
