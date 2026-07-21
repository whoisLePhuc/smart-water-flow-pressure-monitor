#ifndef SWFPM_STORAGE_FACADE_H
#define SWFPM_STORAGE_FACADE_H

#include <stdint.h>
#include <stdbool.h>
#include "port_status.h"

typedef struct {
    struct DataRepository *repo;
    bool initialized;
} StorageFacade;

PortStatus storage_facade_init(StorageFacade *f, void *repo);
PortStatus storage_facade_get_status(StorageFacade *f);

#endif
