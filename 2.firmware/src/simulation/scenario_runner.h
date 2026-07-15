#ifndef SWFPM_SCENARIO_RUNNER_H
#define SWFPM_SCENARIO_RUNNER_H

#include "sim_harness.h"
#include "scenario_manifest.h"
#include "normalized_trace.h"

/* Scenario runner — executes a parsed manifest against a simulation harness,
 * producing a normalized trace. */

typedef struct {
    SimHarness       harness;
    NormalizedTrace  trace;
    RunStatus        final_status;
    uint32_t         turns_executed;
    uint64_t         total_time_us;
} ScenarioResult;

/* Run a manifest through the harness.
 * Returns true if the scenario completed (assertions may have failed). */
bool scenario_run(const ScenarioManifest *manifest, ScenarioResult *result);

/* Check assertions against the result trace.
 * Returns true if all assertions pass. */
bool scenario_check_assertions(const ScenarioManifest *manifest,
                                const ScenarioResult *result,
                                char *fail_msg, uint16_t fail_max);

#endif
