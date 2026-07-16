#ifndef SWFPM_RTC_PORT_H
#define SWFPM_RTC_PORT_H

#include <stdint.h>
#include "ports/port_status.h"

typedef struct {
    void *context;
    PortStatus (*read_epoch_s)(void *context, int64_t *epoch_s);
    PortStatus (*set_epoch_s)(void *context, int64_t epoch_s);
    PortStatus (*set_wakeup_us)(void *context, uint64_t delay_us);
} RtcPort;

#endif /* SWFPM_RTC_PORT_H */
