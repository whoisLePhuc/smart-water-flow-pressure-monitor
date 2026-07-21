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
    const I2cPort *shared_i2c_port; /**< Borrowed; must outlive the composition. */
    FramConfig fram_config;         /**< Configuration for the FM24CL04B. */
    uint32_t storage_io_timeout_us; /**< Per-operation StorageService timeout. */
} AppCompositionDependencies;

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

    bool initialized; /**< True only after the complete object graph is valid. */
} AppComposition;

/**
 * @brief Constructs and connects the complete portable object graph.
 *
 * @param comp Composition instance whose address remains stable at runtime.
 * @param dependencies Borrowed platform ports and immutable configuration.
 *
 * @return true when every manager, driver, port, and service is ready.
 */
bool app_composition_init(
    AppComposition *comp,
    const AppCompositionDependencies *dependencies);

/**
 * @brief Runs bounded cooperative work for all portable modules.
 *
 * The STM32 platform poll must run before this function so IRQ-latched I2C
 * completions are delivered before bus deadlines are evaluated.
 */
void app_composition_poll(AppComposition *comp, uint64_t now_us);

/**
 * @brief Routes one deferred physical I2C completion into the shared bus.
 *
 * This is the completion sink used by the STM32 I2C adapter. Keeping the sink
 * at the composition boundary avoids exposing a second I2cBusManager in
 * main.c or in a platform-specific composition object.
 */
bool app_composition_on_i2c_port_completion(
    AppComposition *comp,
    const I2cPortRequest *request,
    PortStatus result);

#endif /* SWFPM_APP_COMPOSITION_H */
