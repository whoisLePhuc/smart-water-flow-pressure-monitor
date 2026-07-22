#ifndef SWFPM_APP_COMPOSITION_H
#define SWFPM_APP_COMPOSITION_H

#include <stdbool.h>
#include <stdint.h>

#include "drivers/storage/fram_driver.h"
#include "infrastructure/bus/i2c_bus_manager.h"
#include "ports/i2c_port.h"
#include "ports/storage_port.h"
#include "services/storage/storage_service.h"

/**
 * @brief Platform objects and configuration borrowed by AppComposition.
 *
 * The STM32 layer creates the I2cPort after CubeMX has initialized I2C1.
 * AppComposition consumes only the portable port contract and never accesses
 * STM32 HAL handles directly.
 */
typedef struct {
    const I2cPort* shared_i2c_port; /**< Borrowed; must outlive the composition. */
    FramConfig fram_config;         /**< Configuration for the FM24CL04B. */
    uint32_t storage_io_timeout_us; /**< Per-operation StorageService timeout. */
} AppCompositionDependencies;

/**
 * @brief Application-owned startup lifecycle.
 *
 * Persistent volume restore is the first startup prerequisite. The
 * STORAGE_READY state only means that storage produced an accepted OK or EMPTY
 * result. APPLY_VOLUME and READY are reserved for the next integration phase,
 * when the restored checkpoint is applied to the runtime VolumeAccumulator.
 */
typedef enum {
    APP_STARTUP_UNINITIALIZED = 0, /**< Composition has not been constructed. */
    APP_STARTUP_RESTORE_START,     /**< Restore is ready to be started once. */
    APP_STARTUP_RESTORE_WAIT,      /**< Restore is running asynchronously. */
    APP_STARTUP_STORAGE_READY,     /**< Restore result is accepted and retained. */
    APP_STARTUP_APPLY_VOLUME,      /**< Reserved: applying volume to runtime state. */
    APP_STARTUP_READY,             /**< All startup prerequisites are complete. */
    APP_STARTUP_RECOVERY_REQUIRED  /**< Restore failed and recovery is required. */
} AppStartupState;

/**
 * @brief Composition root for all portable objects currently ported to STM32.
 *
 * Add future portable infrastructure, drivers, and services to this structure
 * as they are brought across from 2.firmware. Platform-specific HAL objects do
 * not belong here.
 */
typedef struct {
    /* Shared portable infrastructure. */
    I2cBusManager shared_i2c_bus;

    /* Portable device drivers. */
    FramDriver fram_driver;

    /* Ports exposed by portable drivers. */
    StoragePort fram_storage_port;

    /* Portable services. */
    StorageService storage_service;

    /* Application startup state and retained restore evidence. */
    AppStartupState startup_state;
    StorageRestoreStatus restore_status;
    StorageRestoredVolume restored_volume;
    uint8_t restore_attempts;

    bool initialized;   /**< Complete portable object graph is valid. */
    bool storage_ready; /**< Restore completed with OK or canonical EMPTY. */
    bool runtime_ready; /**< Runtime state is safe to consume production input. */
} AppComposition;

/**
 * @brief Constructs and connects the complete portable object graph.
 *
 * @param comp Composition instance whose address remains stable at runtime.
 * @param dependencies Borrowed platform ports and immutable configuration.
 *
 * @return true when every manager, driver, port, and service is ready.
 */
bool app_composition_init(AppComposition* comp,
                          const AppCompositionDependencies* dependencies);

/**
 * @brief Starts the application-owned asynchronous volume restore.
 *
 * This transition is accepted exactly once from APP_STARTUP_RESTORE_START.
 * Slot selection, decoding, and restore completion remain private to the
 * composition and StorageService; main.c only starts the application.
 *
 * @return true when the restore operation was accepted.
 */
bool app_composition_start(AppComposition* comp);

/**
 * @brief Runs bounded cooperative work for all portable modules.
 *
 * The STM32 platform poll must run before this function so IRQ-latched I2C
 * completions are delivered before bus deadlines are evaluated.
 */
void app_composition_poll(AppComposition* comp, uint64_t now_us);

/**
 * @brief Routes one deferred physical I2C completion into the shared bus.
 *
 * This is the completion sink used by the STM32 I2C adapter. Keeping the sink
 * at the composition boundary avoids exposing a second I2cBusManager in
 * main.c or in a platform-specific composition object.
 */
bool app_composition_on_i2c_port_completion(AppComposition* comp,
                                            const I2cPortRequest* request,
                                            PortStatus result);

#endif /* SWFPM_APP_COMPOSITION_H */
