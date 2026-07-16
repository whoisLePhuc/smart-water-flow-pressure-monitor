#ifndef SWFPM_POWER_FACADE_H
#define SWFPM_POWER_FACADE_H

#include <stdint.h>
#include <stdbool.h>
#include "port_status.h"
#include "domain/power/power_types.h"

typedef struct {
    struct AdcPort       *adc;
    bool initialized;
} PowerFacade;

PortStatus power_facade_init(PowerFacade *f, void *adc);
PortStatus power_facade_get_status(PowerFacade *f);
PowerHealth power_facade_get_health(PowerFacade *f);

#endif
