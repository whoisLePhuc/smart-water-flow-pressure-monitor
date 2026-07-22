#include "app/app_composition.h"

#include <string.h>

static bool dependencies_are_valid(const AppCompositionDependencies* dependencies) {
    if (!dependencies || !dependencies->shared_i2c_port
        || dependencies->storage_io_timeout_us == 0u) {
        return false;
    }

    const I2cPort* port = dependencies->shared_i2c_port;
    return port->context && port->submit && port->cancel && port->recover;
}

bool app_composition_init(AppComposition* comp,
                          const AppCompositionDependencies* dependencies) {
    if (!comp || !dependencies_are_valid(dependencies)) {
        return false;
    }

    memset(comp, 0, sizeof(*comp));

    /* There is exactly one manager for the physical I2C1 bus. */
    i2c_bus_init(&comp->shared_i2c_bus, NULL);
    if (!i2c_bus_bind_port(&comp->shared_i2c_bus, dependencies->shared_i2c_port)) {
        return false;
    }

    if (fram_init(&comp->fram_driver, &comp->shared_i2c_bus, &dependencies->fram_config)
        != STORAGE_IO_SUBMIT_ACCEPTED) {
        return false;
    }

    if (!fram_make_storage_port(&comp->fram_driver, &comp->fram_storage_port)) {
        return false;
    }

    /* StorageService becomes the sole completion owner of this StoragePort. */
    if (StorageService_Init(&comp->storage_service,
                            &comp->fram_storage_port,
                            dependencies->storage_io_timeout_us)
        != STORAGE_OK) {
        return false;
    }

    comp->initialized = true;
    return true;
}

void app_composition_poll(AppComposition* comp, uint64_t now_us) {
    if (!comp || !comp->initialized) {
        return;
    }

    /* A timeout can synchronously notify the active portable driver. */
    (void)i2c_bus_tick(&comp->shared_i2c_bus, now_us);

    /* Advance at most one bounded persistent-storage action per iteration. */
    StorageService_Tick(&comp->storage_service, now_us);
}

bool app_composition_on_i2c_port_completion(AppComposition* comp,
                                            const I2cPortRequest* request,
                                            PortStatus result) {
    if (!comp || !comp->initialized || !request) {
        return false;
    }

    return i2c_bus_on_port_completion(&comp->shared_i2c_bus, request, result);
}
