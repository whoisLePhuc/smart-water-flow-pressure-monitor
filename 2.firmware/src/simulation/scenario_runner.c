#include "scenario_runner.h"
#include "peers/peer_max35103.h"
#include "peers/peer_zssc3241.h"
#include "peers/peer_fram.h"
#include "providers/linux_spi_provider.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Register peers based on manifest configuration */
static bool register_peers(SimHarness *harness, const ScenarioManifest *manifest)
{
    for (uint32_t i = 0; i < manifest->num_peers; i++) {
        const ManifestPeer *mp = &manifest->peers[i];

        switch (mp->type) {
        case PEER_TYPE_MAX35103: {
            /* Register an SPI peer for MAX */
            Max35103Peer *max_peer = (Max35103Peer*)malloc(sizeof(Max35103Peer));
            if (!max_peer) return false;
            max_peer_init(max_peer);

            LinuxSpiPeer spi_peer = {
                .spi_plan = max_peer_plan_spi,
                .context = max_peer,
            };
            linux_spi_register_peer(&harness->spi, spi_peer);
            break;
        }
        case PEER_TYPE_ZSSC3241: {
            Zssc3241Peer *zssc_peer = (Zssc3241Peer*)malloc(sizeof(Zssc3241Peer));
            if (!zssc_peer) return false;
            zssc_peer_init(zssc_peer);

            LinuxI2cPeer i2c_peer = {
                .i2c_plan = zssc_peer_plan_i2c,
                .context = zssc_peer,
            };
            linux_i2c_register_peer(&harness->i2c, mp->i2c_address, i2c_peer);
            break;
        }
        case PEER_TYPE_FRAM: {
            FramPeer *fram_peer = (FramPeer*)malloc(sizeof(FramPeer));
            if (!fram_peer) return false;
            fram_peer_init(fram_peer);

            LinuxI2cPeer i2c_peer = {
                .i2c_plan = fram_peer_plan_i2c,
                .context = fram_peer,
            };
            linux_i2c_register_peer(&harness->i2c, mp->i2c_address, i2c_peer);
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

bool scenario_run(const ScenarioManifest *manifest, ScenarioResult *result)
{
    if (!manifest || !result) return false;

    memset(result, 0, sizeof(*result));
    trace_init(&result->trace);

    /* Initialize harness */
    if (!sim_harness_init(&result->harness)) {
        return false;
    }

    /* Configure run limits from manifest */
    RunControllerLimits limits = {
        .max_turns = (uint32_t)(manifest->max_turns ? manifest->max_turns : 500),
        .max_actions_per_turn = 16,
        .max_same_time_progress_repeats = 5,
        .max_virtual_time_us = manifest->max_time_us ? manifest->max_time_us : 10000000,
    };

    RunController *ctrl = sim_harness_get_controller(&result->harness);
    ctrl->limits = limits;

    /* Register peers */
    if (!register_peers(&result->harness, manifest)) {
        return false;
    }

    /* Run simulation until idle or limit */
    result->final_status = run_controller_until_idle(ctrl);
    result->turns_executed = ctrl->turn_count;
    result->total_time_us = linux_clock_now_us(sim_harness_get_clock(&result->harness));

    return true;
}

bool scenario_check_assertions(const ScenarioManifest *manifest,
                                const ScenarioResult *result,
                                char *fail_msg, uint16_t fail_max)
{
    if (!manifest || !result) return false;

    for (uint32_t i = 0; i < manifest->num_asserts; i++) {
        const ManifestAssert *a = &manifest->asserts[i];

        switch (a->type) {
        case ASSERT_MODE_EQUALS: {
            SystemMode mode = system_fsm_get_context(&result->harness.fsm).current_mode;
            if ((uint32_t)mode != a->expected_value) {
                if (fail_msg)
                    snprintf(fail_msg, fail_max,
                             "Assert %u: mode=%u, expected=%u",
                             i, (uint32_t)mode, a->expected_value);
                return false;
            }
            break;
        }
        case ASSERT_COUNTER_GT: {
            /* Check trace count */
            if (trace_get_count(&result->trace) <= a->expected_value) {
                if (fail_msg)
                    snprintf(fail_msg, fail_max,
                             "Assert %u: trace_count=%u, expected > %u",
                             i, trace_get_count(&result->trace), a->expected_value);
                return false;
            }
            break;
        }
        default:
            break;
        }
    }
    return true;
}
