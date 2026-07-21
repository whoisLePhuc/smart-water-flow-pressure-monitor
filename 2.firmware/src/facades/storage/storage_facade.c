#include "storage_facade.h"

PortStatus storage_facade_init(StorageFacade *f, void *repo)
{
    if (!f) return PORT_STATUS_INVALID_PARAM;
    f->repo = (struct DataRepository *)repo;
    f->initialized = true;
    return PORT_OK;
}

PortStatus storage_facade_get_status(StorageFacade *f)
{
    if (!f || !f->initialized) return PORT_STATUS_UNAVAILABLE;
    return PORT_OK;
}
