#ifndef SWFPM_DOMAIN_DELIVERY_TYPES_H
#define SWFPM_DOMAIN_DELIVERY_TYPES_H

/* =================================================================
 * Domain: connectivity/delivery
 * Owner: domain/connectivity (fw_domain_connectivity)
 * ================================================================= */

#include <stdint.h>

/* ── Delivery outcome ── */

typedef enum {
    DELIVERY_ACKNOWLEDGED,
    DELIVERY_REJECTED_BY_SERVER,
    DELIVERY_TRANSPORT_FAILED,
    DELIVERY_TIMEOUT,
    DELIVERY_OUTCOME_UNKNOWN,
    DELIVERY_CANCELLED
} DeliveryOutcome;

/* ── Telemetry record state ── */

typedef enum {
    TELEREC_BUILT,
    TELEREC_QUEUED,
    TELEREC_IN_FLIGHT,
    TELEREC_ACKNOWLEDGED,
    TELEREC_DROPPED
} TelemetryRecordState;

#endif /* SWFPM_DOMAIN_DELIVERY_TYPES_H */
