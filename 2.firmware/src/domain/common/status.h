#ifndef SWFPM_DOMAIN_STATUS_H
#define SWFPM_DOMAIN_STATUS_H

/* =================================================================
 * Domain: common/status
 * Owner: domain/common (fw_domain_common)
 * ================================================================= */

#include <stdint.h>

/* ── Connectivity status ── */

typedef enum {
    CONN_NOT_READY,
    CONN_CONNECTING,
    CONN_ONLINE,
    CONN_OFFLINE,
    CONN_DEGRADED
} ConnectivityStatus;

/* ── Measurement status ── */

typedef enum {
    MEASUREMENT_STATUS_ACTIVE,
    MEASUREMENT_STATUS_QUIESCED,
    MEASUREMENT_STATUS_DEGRADED,
    MEASUREMENT_STATUS_DISABLED
} MeasurementStatus;

/* ── Orthogonal status set ── */

typedef struct {
    ConnectivityStatus  connectivity;
    MeasurementStatus   measurement;
    uint32_t            storage_status_flags;
    uint32_t            diagnostic_summary_flags;
} OrthogonalStatusSet;

#endif /* SWFPM_DOMAIN_STATUS_H */
