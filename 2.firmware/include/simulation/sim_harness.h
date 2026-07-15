#ifndef SWFPM_SIM_HARNESS_H
#define SWFPM_SIM_HARNESS_H

#include <stdint.h>
#include <stdbool.h>
#include "platform/linux_virtual_clock.h"
#include "platform/linux_scheduled_action_queue.h"
#include "platform/linux_run_controller.h"
#include "platform/providers/linux_spi_provider.h"
#include "platform/providers/linux_i2c_provider.h"
#include "platform/providers/linux_gpio_provider.h"
#include "core/app_event_queue.h"
#include "core/scheduler.h"
#include "core/data_repository.h"
#include "core/system_fsm.h"

/* Simulation harness — composes all Linux backend components
 * and manages the simulation lifecycle. */

typedef struct {
    /* Clock & runtime */
    LinuxVirtualClock           clock;
    LinuxScheduledActionQueue   action_queue;
    RunController               controller;

    /* Platform providers */
    LinuxSpiProvider            spi;
    LinuxI2cProvider            i2c;
    LinuxGpioProvider           gpio;

    /* Firmware core */
    AppEventQueue               event_queue;
    SystemModeManager           fsm;
    DataRepository              repo;

    /* Configuration */
    uint32_t                    boot_generation;
    bool                         initialized;
} SimHarness;

/* ── Lifecycle ──────────────────────────────────────── */

bool sim_harness_init(SimHarness *harness);
void sim_harness_destroy(SimHarness *harness);
bool sim_harness_reset(SimHarness *harness);

/* ── Accessors ──────────────────────────────────────── */

RunController* sim_harness_get_controller(SimHarness *harness);
LinuxVirtualClock* sim_harness_get_clock(SimHarness *harness);
LinuxScheduledActionQueue* sim_harness_get_action_queue(SimHarness *harness);
LinuxSpiProvider* sim_harness_get_spi(SimHarness *harness);
LinuxI2cProvider* sim_harness_get_i2c(SimHarness *harness);
LinuxGpioProvider* sim_harness_get_gpio(SimHarness *harness);
AppEventQueue* sim_harness_get_event_queue(SimHarness *harness);

#endif
