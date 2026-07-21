/**
 * Linux Simulator — Post-Refactor Demo
 *
 * Uses AppComposition for explicit module wiring.
 * Demonstrates: boot → NORMAL → LOW_POWER → WAKE → ERROR
 */

#include "app_composition.h"
#include "queues/app_event_queue.h"
#include "event/app_event.h"
#include "facades/power/power_facade.h"
#include "domain/power/power_config.h"
#include "platform/include/virtual_clock.h"
#include "platform/include/monotonic_clock_port.h"
#include "platform/include/platform_runtime.h"
#include "adc_port_linux.h"
#include "protocols/telemetry/telemetry_builder.h"
#include <stdio.h>
#include <string.h>

static AppComposition comp;
static LinuxAdcAdapter adc_adapter;
static AdcPort adc_port;

static void print_mode(const char *label, SystemMode mode)
{
    static const char *names[] = {
        "INIT", "NORMAL", "LOW_POWER", "SERVICE", "RECOVERY", "ERROR"
    };
    printf("[%-12s] SystemMode = %s\n", label,
           mode < SYSTEM_MODE_COUNT ? names[mode] : "???");
}

static void post_event(AppComposition *c, EventId id, uint32_t gen)
{
    AppEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.id = id;
    evt.source_id = 1;
    evt.priority = EVENT_PRIO_CRITICAL;
    evt.delivery = DELIVERY_EDGE;
    evt.source_generation = gen;
    evt.monotonic_timestamp_us = monotonic_now_us();
    app_event_queue_post(&c->queue, &evt);
}

int main(void)
{
    printf("=== Post-Refactor Simulator ===\n\n");

    platform_init();
    virtual_clock_set_mode(CLOCK_MODE_VIRTUAL);
    virtual_clock_set(0u);
    adc_port_linux_init(&adc_adapter, &adc_port);

    LoopBudgetConfig budget = {
        .max_events_per_turn = 8,
        .max_service_steps = 4,
        .max_exec_us = 0,
    };
    PowerConfig power_config = (PowerConfig)POWER_CONFIG_DEFAULT;
    power_config.sample_period_s = 1u;
    if (!app_composition_init(&comp, &budget, &adc_port, &power_config)) {
        fprintf(stderr, "App composition initialization failed\n");
        return 1;
    }

    print_mode("After init", system_fsm_get_context(&comp.fsm).current_mode);

    post_event(&comp, EVT_INIT_COMPLETED, system_fsm_get_context(&comp.fsm).mode_generation);
    app_event_loop_run_once(&comp.loop);
    print_mode("After INIT_COMPLETED", system_fsm_get_context(&comp.fsm).current_mode);

    post_event(&comp, EVT_LOW_POWER_REQUEST, system_fsm_get_context(&comp.fsm).mode_generation);
    app_event_loop_run_once(&comp.loop);
    print_mode("LP request", system_fsm_get_context(&comp.fsm).current_mode);

    post_event(&comp, EVT_WAKE, system_fsm_get_context(&comp.fsm).mode_generation);
    app_event_loop_run_once(&comp.loop);
    print_mode("After WAKE", system_fsm_get_context(&comp.fsm).current_mode);

    post_event(&comp, EVT_CRITICAL_ERROR, system_fsm_get_context(&comp.fsm).mode_generation);
    app_event_loop_run_once(&comp.loop);
    print_mode("After CRITICAL_ERROR", system_fsm_get_context(&comp.fsm).current_mode);

    {
        adc_port_linux_set_value(&adc_adapter, 1600u);
        virtual_clock_advance(1000000u);
        app_event_loop_run_once(&comp.loop);

        RuntimeSnapshot snapshot;
        TelemetryBuilder builder;
        TelemetryRecord record;
        TelemetryBuilder_Init(&builder);
        if (!data_repository_snapshot_copy(&comp.repo, &snapshot)
            || !TelemetryBuilder_Build(
                &builder, &snapshot, 0u, 0, 0u,
                monotonic_now_us(), 0, 0u, 1u, &record)) {
            fprintf(stderr, "Battery telemetry build failed\n");
            return 1;
        }
        printf("[Battery E2E ] raw=1600 → %umV, telemetry=%umV, health=%u\n",
               snapshot.power.battery_mv, record.battery_mv,
               (unsigned int)snapshot.power.health);

        adc_port_linux_set_value(&adc_adapter, 1300u);
        virtual_clock_advance(1000000u);
        app_event_loop_run_once(&comp.loop);
        if (!data_repository_snapshot_copy(&comp.repo, &snapshot))
            return 1;
        printf("[Battery E2E ] raw=1300 → %umV, health=%u\n",
               snapshot.power.battery_mv,
               (unsigned int)snapshot.power.health);
    }

    printf("\n=== Simulation Complete ===\n");
    return 0;
}
