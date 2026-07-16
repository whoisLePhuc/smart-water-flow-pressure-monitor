#ifndef SWFPM_DOMAIN_SYSTEM_TYPES_H
#define SWFPM_DOMAIN_SYSTEM_TYPES_H


#include <stdint.h>
#include <stdbool.h>
#include "domain/common/metadata.h"


typedef enum {
    SYSTEM_MODE_INIT = 0,
    SYSTEM_MODE_NORMAL,
    SYSTEM_MODE_LOW_POWER,
    SYSTEM_MODE_SERVICE,
    SYSTEM_MODE_RECOVERY,
    SYSTEM_MODE_ERROR,
    SYSTEM_MODE_COUNT
} SystemMode;


typedef struct {
    SystemMode  current_mode;
    uint32_t    mode_generation;
    uint64_t    transition_sequence;
    uint64_t    entered_at_monotonic_us;
    uint32_t    reason_code;
    uint32_t    source_event_id;
    uint64_t    correlation_id;
} SystemModeContext;


typedef struct {
    bool    core_ready;
    bool    flow_readiness_evidence_valid;
    bool    service_ready;
    bool    service_authorized;
    bool    safe_service_boundary;
    bool    safe_to_resume_normal;
    bool    critical_blocker_present;
    bool    wake_sources_armed;
    bool    recovery_can_run;
    bool    return_normal;
    bool    return_service;
    bool    reinitialize_allowed;
    uint32_t blocker_mask;
    uint32_t readiness_generation;
    uint32_t service_session_generation;
    uint32_t recovery_generation;
} ModeGuardContext;

#endif /* SWFPM_DOMAIN_SYSTEM_TYPES_H */
