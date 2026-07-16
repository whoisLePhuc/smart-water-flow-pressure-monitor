#ifndef SWFPM_MODE_GUARD_H
#define SWFPM_MODE_GUARD_H

#include <stdint.h>
#include <stdbool.h>
#include "domain/system/system_types.h"
#include "infrastructure/queues/app_event_queue.h"   /* AppEvent */


typedef struct {
    uint32_t readiness_generation;
    uint32_t service_session_generation;
    uint32_t recovery_generation;
    ModeGuardContext evidence;
} ModeGuardProvider;

void mode_guard_init(ModeGuardProvider *provider);

// Publishes an immutable evidence snapshot for subsequent FSM dispatches.
void mode_guard_publish(ModeGuardProvider *provider,
                        const ModeGuardContext *evidence);

/* Capture an immutable guard context based on current published state.
 * event and current_mode provide context for evidence selection. */
ModeGuardContext mode_guard_capture(
    const ModeGuardProvider *provider,
    const AppEvent *event,
    SystemMode current_mode);

#endif /* SWFPM_MODE_GUARD_H */
