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

static void handle_volume_restore(AppComposition* comp,
                                  StorageRestoreStatus status,
                                  const StorageRestoredVolume* volume) {
    if (!comp || !volume)
        return;

    comp->restore_status = status;
    comp->restored_volume = *volume;
    comp->runtime_ready = false;

    switch (status) {
        case STORAGE_RESTORE_OK:
        case STORAGE_RESTORE_EMPTY:
            comp->storage_ready = true;
            comp->startup_state = APP_STARTUP_STORAGE_READY;
            break;

        case STORAGE_RESTORE_CORRUPT:
        case STORAGE_RESTORE_UNSUPPORTED_SCHEMA:
        case STORAGE_RESTORE_INCOMPATIBLE:
        case STORAGE_RESTORE_SEQUENCE_CONFLICT:
        case STORAGE_RESTORE_IO_ERROR:
        case STORAGE_RESTORE_INTERNAL_ERROR:
        default:
            comp->storage_ready = false;
            comp->startup_state = APP_STARTUP_RECOVERY_REQUIRED;
            break;
    }
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

    comp->restored_volume.selected_slot = SLOT_INDEX_NONE;
    comp->restore_status = STORAGE_RESTORE_INTERNAL_ERROR;
    comp->startup_state = APP_STARTUP_RESTORE_START;
    comp->storage_ready = false;
    comp->runtime_ready = false;
    comp->initialized = true;
    return true;
}

bool app_composition_start(AppComposition* comp) {
    if (!comp || !comp->initialized
        || comp->startup_state != APP_STARTUP_RESTORE_START) {
        return false;
    }

    comp->restore_attempts++;
    StorageStatus status =
        StorageService_StartRestoreVolume(&comp->storage_service);
    if (status != STORAGE_OK) {
        comp->restore_status = STORAGE_RESTORE_INTERNAL_ERROR;
        comp->storage_ready = false;
        comp->runtime_ready = false;
        comp->startup_state = APP_STARTUP_RECOVERY_REQUIRED;
        return false;
    }

    comp->startup_state = APP_STARTUP_RESTORE_WAIT;
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

    if (comp->startup_state == APP_STARTUP_RESTORE_WAIT) {
        StorageRestoreStatus status;
        StorageRestoredVolume volume;
        if (StorageService_TakeRestoredVolume(&comp->storage_service,
                                              &status,
                                              &volume)) {
            handle_volume_restore(comp, status, &volume);
        }
    }

    if (!comp->runtime_ready) {
        return;
    }

    /* Future production scheduler, measurement, and checkpoint polling. */
}

bool app_composition_on_i2c_port_completion(AppComposition* comp,
                                            const I2cPortRequest* request,
                                            PortStatus result) {
    if (!comp || !comp->initialized || !request) {
        return false;
    }

    return i2c_bus_on_port_completion(&comp->shared_i2c_bus, request, result);
}
