#ifndef SWFPM_MODE_GUARD_H
#define SWFPM_MODE_GUARD_H

#include <stdint.h>
#include <stdbool.h>
#include "data_model.h"
#include "app_event_queue.h"   /* AppEvent */

/* =================================================================
 * ModeGuardProvider — captures an immutable guard context snapshot
 * ================================================================= */

typedef struct {
    uint32_t readiness_generation;
    uint32_t service_session_generation;
    uint32_t recovery_generation;
} ModeGuardProvider;

void mode_guard_init(ModeGuardProvider *provider);

/* Capture an immutable guard context based on current published state.
 * event and current_mode provide context for evidence selection. */
ModeGuardContext mode_guard_capture(
    const ModeGuardProvider *provider,
    const AppEvent *event,
    SystemMode current_mode);

#endif /* SWFPM_MODE_GUARD_H */
