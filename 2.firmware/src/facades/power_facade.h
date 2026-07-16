#ifndef SWFPM_POWER_FACADE_H
#define SWFPM_POWER_FACADE_H

#include <stdint.h>
#include "port_status.h"
#include "domain/power/power_types.h"
#include "domain/power/power_config.h"
#include "services/power/power_service.h"
#include "ports/adc_port.h"
#include "infrastructure/queues/app_event_queue.h"
#include "infrastructure/repositories/data_repository.h"

typedef struct {
    PowerService svc;
    PowerConfig  config;
    const AdcPort *adc_port;
    DataRepository *repo;
    AppEventQueue *event_queue;
    bool         initialized;
} PowerFacade;

PortStatus power_facade_init(PowerFacade *f,
                             const PowerConfig *cfg,
                             const AdcPort *adc_port,
                             DataRepository *repo,
                             AppEventQueue *event_queue);
PortStatus power_facade_sample(PowerFacade *f, uint16_t raw_adc);
PortStatus power_facade_process_sample(PowerFacade *f,
                                       uint64_t sample_monotonic_us);
PortStatus power_facade_get_status(const PowerFacade *f);
PowerHealth power_facade_get_health(const PowerFacade *f);
uint16_t  power_facade_get_mv(const PowerFacade *f);

#endif
