#ifndef SWFPM_DOMAIN_STATUS_H
#define SWFPM_DOMAIN_STATUS_H


#include <stdint.h>


typedef enum {
    CONN_NOT_READY,
    CONN_CONNECTING,
    CONN_ONLINE,
    CONN_OFFLINE,
    CONN_DEGRADED
} ConnectivityStatus;


typedef enum {
    MEASUREMENT_STATUS_ACTIVE,
    MEASUREMENT_STATUS_QUIESCED,
    MEASUREMENT_STATUS_DEGRADED,
    MEASUREMENT_STATUS_DISABLED
} MeasurementStatus;


typedef struct {
    ConnectivityStatus  connectivity;
    MeasurementStatus   measurement;
    uint32_t            storage_status_flags;
    uint32_t            diagnostic_summary_flags;
} OrthogonalStatusSet;

#endif /* SWFPM_DOMAIN_STATUS_H */
