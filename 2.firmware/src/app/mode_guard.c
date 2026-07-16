#include "app/mode_guard.h"
#include <string.h>


void mode_guard_init(ModeGuardProvider *provider)
{
    if (!provider)
        return;
    memset(provider, 0, sizeof(*provider));
}

ModeGuardContext mode_guard_capture(
    const ModeGuardProvider *provider,
    const AppEvent *event,
    SystemMode current_mode)
{
    (void)event; /* Context captured from published state, not event */

    /* Phase 1: capture from published state.
     *
     * The guard context is populated by reading published evidence
     * from DataRepository and diagnostic services. In this initial
     * implementation, safe defaults are used — all guards that
     * prevent forward progress are set conservatively.
     *
     * Test code supplies explicit ModeGuardContext directly to
     * system_fsm_dispatch() for full guard testing. */
    ModeGuardContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* Set safe defaults based on current mode */
    switch (current_mode) {
    case SYSTEM_MODE_INIT:
        /* After boot, nothing is ready until explicitly set */
        ctx.core_ready = false;
        ctx.recovery_can_run = true;
        break;

    case SYSTEM_MODE_NORMAL:
        ctx.core_ready = true;
        ctx.flow_readiness_evidence_valid = true;
        break;

    case SYSTEM_MODE_LOW_POWER:
        ctx.wake_sources_armed = true;
        break;

    case SYSTEM_MODE_SERVICE:
        ctx.service_authorized = true;
        ctx.safe_service_boundary = true;
        break;

    case SYSTEM_MODE_RECOVERY:
        ctx.recovery_can_run = true;
        break;

    case SYSTEM_MODE_ERROR:
        ctx.reinitialize_allowed = true;
        break;

    default:
        break;
    }

    if (provider) {
        ctx.readiness_generation = provider->readiness_generation;
        ctx.service_session_generation = provider->service_session_generation;
        ctx.recovery_generation = provider->recovery_generation;
    }
    ctx.safe_to_resume_normal = true;
    ctx.return_normal = true;

    return ctx;
}
