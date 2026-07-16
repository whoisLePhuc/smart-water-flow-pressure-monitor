#include "sim_harness.h"
#include <string.h>

bool sim_harness_init(SimHarness *harness)
{
    if (!harness) return false;

    memset(harness, 0, sizeof(*harness));

    /* Clock */
    linux_clock_init(&harness->clock, LINUX_CLOCK_MODE_DETERMINISTIC);

    /* Action queue */
    action_queue_init(&harness->action_queue);

    /* Platform providers */
    linux_spi_init(&harness->spi, &harness->action_queue);
    linux_i2c_init(&harness->i2c, &harness->action_queue);
    linux_gpio_init(&harness->gpio, &harness->action_queue, 8);

    /* Firmware core */
    AppEventQueueConfig qcfg = {
        .capacity = 32,
        .reserved_critical = 4,
        .reserved_measurement = 4,
    };
    app_event_queue_init(&harness->event_queue, &qcfg);
    scheduler_init(&harness->scheduler);
    system_fsm_init(&harness->fsm);
    data_repository_init(&harness->repo);

    /* Run controller */
    RunControllerLimits limits = {
        .max_turns = 1000,
        .max_actions_per_turn = 16,
        .max_same_time_progress_repeats = 5,
        .max_virtual_time_us = 100000000ULL,
    };
    run_controller_init(&harness->controller,
                         &harness->clock, &harness->action_queue,
                         &harness->event_queue,
                         &harness->scheduler,
                         &harness->fsm, &harness->repo,
                         &limits);

    harness->boot_generation = 1;
    harness->initialized = true;
    return true;
}

void sim_harness_destroy(SimHarness *harness)
{
    if (!harness) return;
    memset(harness, 0, sizeof(*harness));
}

bool sim_harness_reset(SimHarness *harness)
{
    if (!harness) return false;
    uint32_t next_gen = harness->boot_generation + 1;
    sim_harness_destroy(harness);
    if (!sim_harness_init(harness)) return false;
    harness->boot_generation = next_gen;
    return true;
}

RunController* sim_harness_get_controller(SimHarness *harness)
{
    return harness ? &harness->controller : NULL;
}

LinuxVirtualClock* sim_harness_get_clock(SimHarness *harness)
{
    return harness ? &harness->clock : NULL;
}

LinuxScheduledActionQueue* sim_harness_get_action_queue(SimHarness *harness)
{
    return harness ? &harness->action_queue : NULL;
}

LinuxSpiProvider* sim_harness_get_spi(SimHarness *harness)
{
    return harness ? &harness->spi : NULL;
}

LinuxI2cProvider* sim_harness_get_i2c(SimHarness *harness)
{
    return harness ? &harness->i2c : NULL;
}

LinuxGpioProvider* sim_harness_get_gpio(SimHarness *harness)
{
    return harness ? &harness->gpio : NULL;
}

AppEventQueue* sim_harness_get_event_queue(SimHarness *harness)
{
    return harness ? &harness->event_queue : NULL;
}
