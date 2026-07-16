#ifndef SWFPM_CONNECTIVITY_FACADE_H
#define SWFPM_CONNECTIVITY_FACADE_H

#include <stdint.h>
#include <stdbool.h>
#include "port_status.h"

typedef struct {
    struct DataRepository *repo;
    struct AppEventQueue  *queue;
    bool initialized;
} ConnectivityFacade;

PortStatus connectivity_facade_init(ConnectivityFacade *f, void *repo, void *queue);
PortStatus connectivity_facade_get_status(ConnectivityFacade *f);

#endif
