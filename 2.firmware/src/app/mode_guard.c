#include "app/mode_guard.h"
#include <string.h>


void mode_guard_init(ModeGuardProvider *provider)
{
    if (!provider)
        return;
    memset(provider, 0, sizeof(*provider));
}

void mode_guard_publish(ModeGuardProvider *provider,
                        const ModeGuardContext *evidence)
{
    if (!provider || !evidence) return;
    provider->evidence = *evidence;
    provider->readiness_generation++;
    provider->service_session_generation++;
    provider->recovery_generation++;
}

ModeGuardContext mode_guard_capture(
    const ModeGuardProvider *provider,
    const AppEvent *event,
    SystemMode current_mode)
{
    (void)event;
    (void)current_mode;
    ModeGuardContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (provider) {
        ctx = provider->evidence;
        ctx.readiness_generation = provider->readiness_generation;
        ctx.service_session_generation = provider->service_session_generation;
        ctx.recovery_generation = provider->recovery_generation;
    }
    return ctx;
}
