#ifndef SWFPM_DOMAIN_MEASUREMENT_TYPES_H
#define SWFPM_DOMAIN_MEASUREMENT_TYPES_H

/* =================================================================
 * Domain: measurement
 * Owner: domain/measurement (fw_domain_measurement)
 *
 * Measurement result types for flow, temperature, pressure sensors.
 * These results are produced by the measurement service after raw
 * sensor data processing and calibration.
 * ================================================================= */

#include "domain/common/metadata.h"

/* ── Flow direction ── */

typedef enum {
    FLOW_DIRECTION_FORWARD,
    FLOW_DIRECTION_REVERSE,
    FLOW_DIRECTION_NONE
} FlowDirection;

/* ── Temperature result ── */

typedef struct {
    ResultMetadata  meta;
    int32_t         temperature_mdeg_c;     /* milli-degrees Celsius */
    uint32_t        processing_flags;
} TemperatureResult;

/* ── Flow result ── */

typedef struct {
    ResultMetadata  meta;
    int64_t         flow_ul_per_s;           /* microlitres/second, signed */
    FlowDirection   direction;
    uint32_t        compensation_flags;
    uint32_t        processing_flags;
    uint64_t        paired_temperature_sequence;
} FlowResult;

/* ── Pressure result ── */

typedef struct {
    ResultMetadata  meta;
    int32_t         pressure_pa;             /* Pascals */
    uint32_t        processing_flags;
} PressureResult;

#endif /* SWFPM_DOMAIN_MEASUREMENT_TYPES_H */
