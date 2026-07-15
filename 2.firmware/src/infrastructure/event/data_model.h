#ifndef SWFPM_DATA_MODEL_H
#define SWFPM_DATA_MODEL_H

#include <stdint.h>
#include <stdbool.h>

/* =================================================================
 * Metadata types (FW-CORE-005 v0.2)
 * ================================================================= */

typedef enum {
    DATA_VALID,
    DATA_INVALID,
    DATA_UNAVAILABLE
} DataValidity;

typedef enum {
    DATA_FRESH,
    DATA_STALE,
    DATA_FRESHNESS_UNKNOWN
} DataFreshness;

typedef enum {
    DATA_ACCEPTED,
    DATA_DEGRADED_NOT_ACCEPTED,
    DATA_REJECTED
} ProductionAcceptance;

/* Three independent dimensions for result classification.
 * Production admission requires:
 *   purpose=PRODUCTION, origin=LIVE_DEVICE, provenance=MEASURED
 */
typedef enum {
    MEAS_PURPOSE_BOOT_SELF_CHECK,
    MEAS_PURPOSE_PRODUCTION,
    MEAS_PURPOSE_SERVICE,
    MEAS_PURPOSE_CALIBRATION,
    MEAS_PURPOSE_DIAGNOSTIC,
    MEAS_PURPOSE_RECOVERY_VERIFY
} MeasurementPurpose;

typedef enum {
    DATA_ORIGIN_LIVE_DEVICE,
    DATA_ORIGIN_SIMULATED_DEVICE,
    DATA_ORIGIN_REPLAYED_FIXTURE
} DataOrigin;

typedef enum {
    PROVENANCE_MEASURED,
    PROVENANCE_RESTORED,
    PROVENANCE_DEFAULTED,
    PROVENANCE_ESTIMATED
} DataProvenance;

typedef enum {
    TIME_QUALITY_VALID,
    TIME_QUALITY_INVALID,
    TIME_QUALITY_ESTIMATED,
    TIME_QUALITY_UNKNOWN
} TimeQuality;

/* Binding reference — traces calibration/config/profile origin for each result */
typedef struct {
    uint32_t binding_id;
    uint32_t binding_version;
    uint32_t profile_version;
} MeasurementBindingReference;

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

/* =================================================================
 * Event payload types (FW-CORE-003 v0.2)
 * ================================================================= */

/* SPI transaction completion — MAX35103 result mailbox */
typedef struct {
    uint32_t correlation_id;
    uint32_t source_generation;
    uint16_t rx_length;
    uint8_t  status_flags;
} MaxSpiCompletionPayload;

/* MAX raw measurement ready — coherent immutable mailbox */
typedef struct {
    uint64_t sample_sequence;
    uint64_t capture_monotonic_us;
    uint32_t source_generation;
    uint32_t config_version;
    uint16_t tof_words[4];
    uint8_t  status_bits;
    uint8_t  quality_flags;
} MaxRawReadyPayload;

/* I2C transaction completion — generic shared-bus terminal */
typedef struct {
    uint32_t client_id;
    uint32_t transaction_id;
    uint32_t correlation_id;
    uint32_t client_generation;
    uint32_t bus_generation;
    uint8_t  status;
    uint8_t  rx_buffer[8];
    uint8_t  rx_length;
} I2cTransactionCompletionPayload;

/* Pressure raw measurement ready — ZSSC3241 result */
typedef struct {
    uint64_t sample_sequence;
    uint64_t capture_monotonic_us;
    uint32_t source_generation;
    uint32_t profile_version;
    int32_t  raw_count;
    uint8_t  status_bits;
} PressureRawReadyPayload;

/* =================================================================
 * Measurement result types
 * ================================================================= */

typedef enum {
    FLOW_DIRECTION_FORWARD,
    FLOW_DIRECTION_REVERSE,
    FLOW_DIRECTION_NONE
} FlowDirection;

typedef struct {
    ResultMetadata  meta;
    int32_t         temperature_mdeg_c;     /* milli-degrees Celsius */
    uint32_t        processing_flags;
} TemperatureResult;

typedef struct {
    ResultMetadata  meta;
    int64_t         flow_ul_per_s;           /* microlitres/second, signed */
    FlowDirection   direction;
    uint32_t        compensation_flags;
    uint32_t        processing_flags;
    uint64_t        paired_temperature_sequence;
} FlowResult;

typedef struct {
    ResultMetadata  meta;
    int32_t         pressure_pa;             /* Pascals */
    uint32_t        processing_flags;
} PressureResult;

/* =================================================================
 * Product state types
 * ================================================================= */

typedef struct {
    uint64_t    state_version;
    uint64_t    total_volume_ul;             /* microlitres */
    uint64_t    last_consumed_flow_sequence;
    uint64_t    updated_monotonic_us;
    uint64_t    checkpointed_volume_ul;
    uint64_t    checkpoint_sequence;
    uint32_t    config_version;
    uint32_t    flags;
} VolumeState;

typedef enum {
    LEAK_STATE_UNKNOWN,
    LEAK_STATE_NORMAL,
    LEAK_STATE_SUSPECTED,
    LEAK_STATE_CONFIRMED
} LeakState;

typedef enum {
    LEAK_EVAL_NOT_EVALUATED,
    LEAK_EVAL_INSUFFICIENT_DATA,
    LEAK_EVAL_EVALUATING,
    LEAK_EVAL_COMPLETED
} LeakEvaluationStatus;

typedef struct {
    uint64_t    result_version;
    LeakState              state;
    LeakEvaluationStatus   evaluation_status;
    uint16_t    confidence_permille;         /* 0..1000 */
    uint32_t    reason_flags;
    uint32_t    evidence_flags;
    uint64_t    state_entered_monotonic_us;
    uint64_t    latest_evidence_monotonic_us;
    uint32_t    profile_version;
    uint32_t    config_version;
} LeakDetectionResult;

/* =================================================================
 * System mode types
 * ================================================================= */

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

/* =================================================================
 * Mode guard context
 * ================================================================= */

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

/* =================================================================
 * Orthogonal status enums
 * ================================================================= */

typedef enum {
    CONNECTIVITY_ONLINE,
    CONNECTIVITY_OFFLINE,
    CONNECTIVITY_UNKNOWN
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

/* =================================================================
 * RuntimeSnapshot — full system state view
 * ================================================================= */

typedef struct {
    uint32_t    schema_version;
    uint64_t    snapshot_version;
    uint64_t    publish_monotonic_us;
    int64_t     publish_wall_time_s;
    TimeQuality publish_time_quality;

    SystemModeContext    mode;
    OrthogonalStatusSet  statuses;

    TemperatureResult    temperature;
    FlowResult           flow;
    PressureResult       pressure;
    VolumeState          volume;
    LeakDetectionResult  leak;

    uint32_t    active_config_version;
    uint32_t    active_calibration_version;
    uint32_t    diagnostic_summary_flags;
} RuntimeSnapshot;

/* =================================================================
 * Event ID catalog (FW-CORE-003 v0.2 canonical catalog)
 * ================================================================= */

typedef enum {
    /* System/mode */
    EVT_SYSTEM_START                 = 0x0100,
    EVT_INIT_COMPLETED               = 0x0101,
    EVT_RECOVERABLE_INIT_FAILURE     = 0x0102,
    EVT_CRITICAL_INIT_FAILURE        = 0x0103,
    EVT_LOW_POWER_REQUEST            = 0x0104,
    EVT_WAKE                         = 0x0105,
    EVT_SERVICE_REQUEST              = 0x0106,
    EVT_SERVICE_EXIT                 = 0x0107,
    EVT_SYSTEM_RECOVERY_REQUIRED     = 0x0108,
    EVT_RECOVERY_SUCCEEDED           = 0x0109,
    EVT_RECOVERY_FAILED              = 0x010A,
    EVT_CRITICAL_ERROR               = 0x010B,
    EVT_AUTHORIZED_RECOVERY_REQUEST  = 0x010C,
    EVT_CONTROLLED_REINITIALIZE      = 0x010D,

    /* Measurement — MAX35103 (HW ingress → SPI → raw ready) */
    EVT_MAX_IRQ_ASSERTED             = 0x0200,
    EVT_MAX_SPI_COMPLETED            = 0x0201,
    EVT_MAX_SPI_FAILED               = 0x0202,
    EVT_MAX_RAW_READY                = 0x0203,
    EVT_MAX_RESULT_TIMEOUT           = 0x0204,

    /* Measurement — flow & temperature processing */
    EVT_FLOW_PROCESSING_COMPLETED    = 0x0205,
    EVT_TEMPERATURE_RESULT_READY     = 0x0206,
    EVT_FLOW_RESULT_READY            = 0x0207,

    /* Measurement — ZSSC3241 (one-shot → EOC → raw → processing) */
    EVT_PRESSURE_SAMPLE_DUE          = 0x0208,
    EVT_PRESSURE_EOC_ASSERTED        = 0x0209,
    EVT_PRESSURE_POLL_DUE            = 0x020A,
    EVT_PRESSURE_RAW_READY           = 0x020B,
    EVT_PRESSURE_TIMEOUT             = 0x020C,
    EVT_PRESSURE_RESULT_READY        = 0x020D,
    EVT_MEASUREMENT_STATUS_CHANGED   = 0x020E,

    /* Product/data */
    EVT_VOLUME_UPDATED               = 0x0300,
    EVT_VOLUME_CHECKPOINT_REQUIRED   = 0x0301,
    EVT_LEAK_RESULT_UPDATED          = 0x0302,
    EVT_LEAK_STATE_CHANGED           = 0x0303,
    EVT_SNAPSHOT_PUBLISH_REQUESTED   = 0x0304,
    EVT_SNAPSHOT_PUBLISHED           = 0x0305,

    /* Infrastructure/bus — generic shared-bus terminal events */
    EVT_I2C_TRANSACTION_COMPLETED    = 0x0380,
    EVT_I2C_TRANSACTION_FAILED       = 0x0381,

    /* Configuration/storage */
    EVT_CONFIG_CANDIDATE_READY       = 0x0400,
    EVT_CONFIG_COMMIT_REQUIRED       = 0x0401,
    EVT_CONFIG_COMMIT_COMPLETED      = 0x0402,
    EVT_CONFIG_COMMIT_FAILED         = 0x0403,
    EVT_CONFIG_APPLY_REQUESTED       = 0x0404,
    EVT_CONFIG_APPLY_RESULT          = 0x0405,
    EVT_STORAGE_COMMIT_REQUESTED     = 0x0406,
    EVT_STORAGE_COMMIT_COMPLETED     = 0x0407,
    EVT_STORAGE_COMMIT_FAILED        = 0x0408,

    /* Time/reporting */
    EVT_RTC_ALARM                    = 0x0500,
    EVT_TIME_SYNC_RECEIVED           = 0x0501,
    EVT_TIME_VALIDITY_CHANGED        = 0x0502,
    EVT_TIME_GENERATION_CHANGED      = 0x0503,
    EVT_REPORT_DUE                   = 0x0504,

    /* BLE/cellular */
    EVT_BLE_RX_AVAILABLE            = 0x0600,
    EVT_BLE_REQUEST_READY           = 0x0601,
    EVT_BLE_TX_COMPLETED            = 0x0602,
    EVT_CELLULAR_RX_AVAILABLE       = 0x0603,
    EVT_CELLULAR_STEP_DUE           = 0x0604,
    EVT_CONNECTIVITY_CHANGED        = 0x0605,
    EVT_TELEMETRY_RECORD_ENQUEUED   = 0x0606,
    EVT_TELEMETRY_RETRY_DUE         = 0x0607,
    EVT_TELEMETRY_DELIVERY_CONFIRMED = 0x0608,
    EVT_TELEMETRY_DELIVERY_FAILED   = 0x0609,

    /* Display/power/health */
    EVT_LCD_REFRESH_REQUESTED       = 0x0700,
    EVT_LCD_UPDATE_COMPLETED        = 0x0701,
    EVT_LCD_UPDATE_FAILED           = 0x0702,
    EVT_POWER_STATUS_CHANGED        = 0x0703,
    EVT_HEALTH_CHECK_DUE            = 0x0704,
    EVT_WATCHDOG_PROGRESS_REQUIRED  = 0x0705,
} EventId;

#endif /* SWFPM_DATA_MODEL_H */
