#include "connectivity_facade.h"

PortStatus connectivity_facade_init(ConnectivityFacade *f, void *repo, void *queue)
{
    if (!f) return PORT_STATUS_INVALID_PARAM;
    f->repo = (struct DataRepository *)repo;
    f->queue = (struct AppEventQueue  *)queue;
    f->initialized = true;
    return PORT_OK;
}

PortStatus connectivity_facade_get_status(ConnectivityFacade *f)
{
    if (!f || !f->initialized) return PORT_STATUS_UNAVAILABLE;
    return PORT_OK;
}
