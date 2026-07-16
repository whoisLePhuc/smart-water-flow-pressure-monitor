#ifndef SWFPM_DOMAIN_METADATA_H
#define SWFPM_DOMAIN_METADATA_H

/* =================================================================
 * Domain: common/metadata
 * Owner: domain/common (fw_domain_common)
 *
 * Canonical metadata and classification types used across all
 * measurement and product domains. These are the fundamental
 * type definitions — any type that needs to classify data quality,
 * purpose, origin, or provenance uses these enums.
 *
 * @deprecated Do NOT include individual struct/enum from here in
 * new domain code. Include this header only when using multiple
 * metadata types together (e.g., ResultMetadata).
 * ================================================================= */

#include <stdint.h>
#include <stdbool.h>

/* ── Data validity ── */

typedef enum {
    DATA_VALID,
    DATA_INVALID,
    DATA_UNAVAILABLE
} DataValidity;

/* ── Data freshness ── */

typedef enum {
    DATA_FRESH,
    DATA_STALE,
    DATA_FRESHNESS_UNKNOWN
} DataFreshness;

/* ── Production acceptance ── */

typedef enum {
    DATA_ACCEPTED,
    DATA_DEGRADED_NOT_ACCEPTED,
    DATA_REJECTED
} ProductionAcceptance;

/* ── Measurement purpose ── */

typedef enum {
    MEAS_PURPOSE_BOOT_SELF_CHECK,
    MEAS_PURPOSE_PRODUCTION,
    MEAS_PURPOSE_SERVICE,
    MEAS_PURPOSE_CALIBRATION,
    MEAS_PURPOSE_DIAGNOSTIC,
    MEAS_PURPOSE_RECOVERY_VERIFY
} MeasurementPurpose;

/* ── Data origin ── */

typedef enum {
    DATA_ORIGIN_LIVE_DEVICE,
    DATA_ORIGIN_SIMULATED_DEVICE,
    DATA_ORIGIN_REPLAYED_FIXTURE
} DataOrigin;

/* ── Data provenance ── */

typedef enum {
    PROVENANCE_MEASURED,
    PROVENANCE_RESTORED,
    PROVENANCE_DEFAULTED,
    PROVENANCE_ESTIMATED
} DataProvenance;

/* ── Time quality ── */

typedef enum {
    TIME_QUALITY_VALID,
    TIME_QUALITY_INVALID,
    TIME_QUALITY_ESTIMATED,
    TIME_QUALITY_UNKNOWN
} TimeQuality;

/* ── System time quality ── */

typedef enum {
    SYS_TIME_INVALID,
    SYS_TIME_RTC_HOLDOVER,
    SYS_TIME_NETWORK_SYNCED
} SystemTimeQuality;

/* ── Measurement binding ── */

typedef struct {
    uint32_t binding_id;
    uint32_t binding_version;
    uint32_t profile_version;
} MeasurementBindingReference;

/* ── Result metadata ──
 * Three independent classification dimensions:
 *   purpose / origin / provenance
 * Production admission requires:
 *   purpose=PRODUCTION, origin=LIVE_DEVICE, provenance=MEASURED */

typedef struct {
    uint32_t    source_id;
    uint32_t    source_generation;
    uint64_t    sample_sequence;
    uint64_t    result_version;
    uint64_t    sample_monotonic_us;
    uint64_t    completion_monotonic_us;
    int64_t     wall_time_s;
    uint32_t    config_version;
    uint32_t    calibration_version;
    uint32_t    reason_flags;
    DataValidity           validity;
    DataFreshness              freshness;
    ProductionAcceptance       acceptance;
    MeasurementPurpose         purpose;
    DataOrigin                 origin;
    DataProvenance             provenance;
    MeasurementBindingReference binding;
    TimeQuality                time_quality;
} ResultMetadata;

static inline bool result_metadata_is_production(const ResultMetadata *meta)
{
    return meta
        && meta->purpose == MEAS_PURPOSE_PRODUCTION
        && meta->origin == DATA_ORIGIN_LIVE_DEVICE
        && meta->provenance == PROVENANCE_MEASURED;
}

#endif /* SWFPM_DOMAIN_METADATA_H */
