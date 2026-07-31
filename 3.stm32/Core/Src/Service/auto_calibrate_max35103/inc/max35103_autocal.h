/**
 ******************************************************************************
 * @file    max35103_autocal.h
 * @brief   Portable MAX35103 acoustic-profile auto-tuning service
 ******************************************************************************
 *
 * This module searches for a stable receive profile and creates independent
 * verification evidence. It does not convert TOF to flow and does not write
 * MAX35103 configuration flash.
 *
 * One call to MAX35103_AutoCalStep() evaluates at most one measurement. The
 * caller therefore retains control of watchdog feeding, cancellation, logging,
 * and power policy. The injected backend also makes the search logic host
 * testable without STM32 HAL.
 ******************************************************************************
 */

#ifndef SWFPM_MAX35103_AUTOCAL_H
#define SWFPM_MAX35103_AUTOCAL_H

#include <stdbool.h>
#include <stdint.h>

#include "max35103.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX35103_AUTOCAL_REPORT_MAGIC          UINT32_C(0x4D43414C)
#define MAX35103_AUTOCAL_REPORT_VERSION        8U
#define MAX35103_AUTOCAL_PERTURBATION_COUNT    8U
#define MAX35103_AUTOCAL_DISCOVERY_FINALISTS   4U
#define MAX35103_AUTOCAL_SAMPLE_WORKSPACE_SIZE 128U

#define MAX35103_AUTOCAL_SEED_DEFAULT                   \
    {                                                   \
        .profile_id = 1U,                               \
        .profile_version = 1U,                          \
        .event_mode_cmd = MAX35103_CMD_EVTMG2,          \
        .tof1 = 0x1813U,                                \
        .tof2 = 0x4201U,                                \
        .tof3 = 0x0506U,                                \
        .tof4 = 0x0708U,                                \
        .tof5 = 0x090AU,                                \
        .tof6 = 0xFC0DU,                                \
        .tof7 = 0xFC0AU,                                \
        .event_timing_1 = 0x0100U,                      \
        .event_timing_2 = MAX35103_EVT2_TEMP_T1_T3,     \
        .tof_measurement_delay = 0x0021U,               \
        .calibration_control = MAX35103_CAL_CTRL_INT_EN,\
        .init_timeout_ms = 20U,                         \
        .result_timeout_ms = 20U,                       \
        .halt_timeout_ms = 20U,                         \
        .reference_resistance_milliohm = 1000000U,      \
        .rtd_nominal_resistance_milliohm = 100000U,     \
    }

/**
 * @brief Stages and terminal states of the auto-calibration state machine.
 */
typedef enum
{
    MAX35103_AUTOCAL_STATE_IDLE = 0,
    MAX35103_AUTOCAL_STATE_DISCOVERY,
    MAX35103_AUTOCAL_STATE_BIAS_CHARGE,
    MAX35103_AUTOCAL_STATE_DLY_FINE,
    MAX35103_AUTOCAL_STATE_OFFSET_UP_COARSE,
    MAX35103_AUTOCAL_STATE_OFFSET_UP_FINE,
    MAX35103_AUTOCAL_STATE_OFFSET_DOWN_COARSE,
    MAX35103_AUTOCAL_STATE_OFFSET_DOWN_FINE,
    MAX35103_AUTOCAL_STATE_WAVE_SELECT,
    MAX35103_AUTOCAL_STATE_RETURN_UP,
    MAX35103_AUTOCAL_STATE_RETURN_DOWN,
    MAX35103_AUTOCAL_STATE_VERIFY,
    MAX35103_AUTOCAL_STATE_ROBUSTNESS,
    MAX35103_AUTOCAL_STATE_RESET_VERIFY,
    MAX35103_AUTOCAL_STATE_COMPLETE,
    MAX35103_AUTOCAL_STATE_FAILED,
    MAX35103_AUTOCAL_STATE_CANCELLED,
} Max35103AutoCalState;

/**
 * @brief Status values returned by the auto-calibration service.
 */
typedef enum
{
    MAX35103_AUTOCAL_OK = 0,
    MAX35103_AUTOCAL_RUNNING = 1,
    MAX35103_AUTOCAL_COMPLETE = 2,
    MAX35103_AUTOCAL_INVALID_ARG = -1,
    MAX35103_AUTOCAL_DRIVER_ERROR = -2,
    MAX35103_AUTOCAL_NO_CANDIDATE = -3,
    MAX35103_AUTOCAL_CANCELLED = -4,
} Max35103AutoCalStatus;

/**
 * @brief Confidence level assigned to a completed calibration report.
 */
typedef enum
{
    MAX35103_AUTOCAL_CONFIDENCE_NONE = 0,
    MAX35103_AUTOCAL_CONFIDENCE_CANDIDATE,
    MAX35103_AUTOCAL_CONFIDENCE_ACOUSTIC_VERIFIED,
    MAX35103_AUTOCAL_CONFIDENCE_ZERO_FLOW_COMPENSATED,
} Max35103AutoCalConfidence;

/**
 * Hardware-independent operations used by the search service.
 *
 * configure() must apply a volatile profile and verify readback. measure()
 * must perform one TOF_DIFF operation and return both averaged and per-wave
 * evidence from that same result. reset() must restore the device to a ready
 * state; the service calls configure() again after every reset.
 */
typedef struct
{
    Max35103Status (*configure)(void *context, const Max35103Profile *profile);
    Max35103Status (*measure)(void *context, Max35103RawResult *result, Max35103WaveEvidence *wave);
    Max35103Status (*reset)(void *context);
    void *context;
} Max35103AutoCalBackend;

/**
 * Search ranges and validation gates.
 *
 * Search steps are inclusive. CT mask bit n enables CT=n in both Discovery
 * and BIAS_CHARGE. The initial comparator offset is unsigned 0..127; the
 * return offset is signed int8. samples_per_candidate, finalist_samples and
 * verification_samples must fit the caller-provided workspace.
 */
typedef struct
{
    uint32_t acoustic_path_length_um;
    /*
     * Accepted wave-zero arrival window. The service removes the programmed
     * HIT wave-number delay before applying this gate. Board-specific fixed
     * analog/circuit delay may still be included by adjusting this window.
     */
    int64_t expected_min_tof_ps;
    int64_t expected_max_tof_ps;

    uint8_t dpl_min;
    uint8_t dpl_max;
    uint8_t pulse_count_min;
    uint8_t pulse_count_max;
    uint8_t pulse_count_step;
    uint8_t ct_mask;
    bool try_both_polarities;

    uint16_t dly_min;
    uint16_t dly_max;
    uint16_t dly_coarse_step;
    uint16_t dly_fine_step;

    uint8_t initial_offset_min;
    uint8_t initial_offset_max;
    uint8_t initial_offset_coarse_step;
    uint8_t initial_offset_fine_step;
    int8_t return_offset_min;
    int8_t return_offset_max;
    uint8_t return_offset_step;

    uint8_t t2_wave_min;
    uint8_t t2_wave_max;
    uint8_t hit_count_min;
    uint8_t hit_count_max;

    uint16_t samples_per_candidate;
    /* Longer batches used by WAVE_SELECT and comparator-return tuning. */
    uint16_t finalist_samples;
    uint16_t verification_samples;
    uint16_t min_valid_rate_per_mille;
    /* Used by BIAS/DLY/OFFSET/WAVE/RETURN search stages. */
    uint16_t min_tuning_physical_rate_per_mille;
    /* Used by Discovery, Verify, Robustness and Reset Verify. */
    uint16_t min_physical_rate_per_mille;
    uint16_t min_wave_valid_rate_per_mille;
    uint16_t min_wvr_good_rate_per_mille;
    /*
     * WVR packs two different ratios.  Keep separate lower bounds because
     * the early-edge t1 pulse can legitimately be much narrower than t2,
     * while t2/tideal must still remain representative of the launch period.
     */
    uint8_t wvr_t1_t2_min_q7;
    uint8_t wvr_t2_ideal_min_q7;
    uint8_t wvr_ratio_max_q7;

    int64_t max_tof_mad_ps;
    int64_t max_diff_mad_ps;
    int64_t max_period_error_ps;
    /*
     * Maximum median UP/DOWN separation.  This rejects profiles that lock the
     * two directions to different acoustic cycles while both remain inside
     * the broad physical arrival window.
     */
    int64_t max_direction_delta_ps;
    uint16_t max_cycle_slips;
    uint16_t max_cycle_slip_rate_per_mille;
    uint8_t required_perturbation_passes;
    uint8_t max_consecutive_driver_errors;
    uint8_t max_stage_retries;
    uint8_t max_profile_fallbacks;
    uint16_t max_busy_polls;

    bool zero_flow_confirmed;
} Max35103AutoCalConfig;

/**
 * Caller-owned workspace entry. Use sizeof(Max35103AutoCalSample) when
 * budgeting RAM because alignment differs by compiler; a typical 32/64-bit
 * ABI uses 48 bytes per sample (6 KiB for 128 samples).
 */
typedef struct
{
    /*
     * UP/DOWN are wave-zero arrival estimates, normalized from the configured
     * HITx wave numbers. DIFF remains the device AVGUP-AVGDN result.
     */
    int64_t tof_up_ps;
    int64_t tof_down_ps;
    int64_t tof_diff_ps;
    int64_t period_error_ps;
    int64_t work_ps;
    uint8_t flags;
} Max35103AutoCalSample;

#define MAX35103_AUTOCAL_SAMPLE_VALID       0x01U
#define MAX35103_AUTOCAL_SAMPLE_PHYSICAL    0x02U
#define MAX35103_AUTOCAL_SAMPLE_WAVE_VALID  0x04U
#define MAX35103_AUTOCAL_SAMPLE_WVR_UP_GOOD 0x08U
#define MAX35103_AUTOCAL_SAMPLE_WVR_DN_GOOD 0x10U
#define MAX35103_AUTOCAL_SAMPLE_WVR_GOOD    0x20U

/**
 * @brief Aggregated statistics, gate results, and score for one candidate.
 */
typedef struct
{
    uint16_t attempted_count;
    uint16_t valid_count;
    uint16_t physical_count;
    uint16_t wave_valid_count;
    uint16_t wvr_up_good_count;
    uint16_t wvr_down_good_count;
    uint16_t wvr_good_count;
    uint16_t timeout_count;
    uint16_t cycle_slip_count;
    uint16_t valid_rate_per_mille;
    uint16_t physical_rate_per_mille;
    uint16_t physical_rate_required_per_mille;
    uint16_t wave_valid_rate_per_mille;
    uint16_t wvr_up_good_rate_per_mille;
    uint16_t wvr_down_good_rate_per_mille;
    uint16_t wvr_good_rate_per_mille;
    uint16_t cycle_slip_rate_per_mille;

    int64_t median_tof_up_ps;
    int64_t median_tof_down_ps;
    int64_t median_tof_diff_ps;
    int64_t direction_delta_ps;
    int64_t mad_tof_up_ps;
    int64_t mad_tof_down_ps;
    int64_t mad_tof_diff_ps;
    int64_t median_period_error_ps;
    int64_t mad_period_error_ps;

    uint64_t score;
    bool communication_gate;
    bool direction_gate;
    bool physical_gate;
    bool period_gate;
    bool waveform_gate;
    bool stage_waveform_gate;
    bool statistics_gate;
    bool passed;
} Max35103AutoCalMetrics;

/**
 * @brief Persistent evidence report produced by a completed calibration.
 */
typedef struct
{
    uint32_t magic;
    uint16_t report_version;
    uint16_t report_size;
    uint32_t acoustic_path_length_um;
    int64_t expected_min_tof_ps;
    int64_t expected_max_tof_ps;

    Max35103Profile selected_profile;
    Max35103AutoCalMetrics verification;

    uint32_t evaluated_candidate_count;
    uint32_t attempted_measurement_count;
    uint16_t perturbation_tested;
    uint16_t perturbation_passed;
    uint8_t profile_fallbacks_used;
    int64_t zero_flow_offset_ps;
    int64_t zero_flow_mad_ps;
    Max35103AutoCalConfidence confidence;
    bool reset_verified;
    uint32_t evidence_crc32;
} Max35103AutoCalReport;

/**
 * @brief Read-only progress snapshot for diagnostics and user interfaces.
 */
typedef struct
{
    Max35103AutoCalState state;
    uint32_t candidate_index;
    uint32_t candidate_count;
    uint16_t sample_index;
    uint16_t sample_target;
    uint8_t stage_retry_count;
    uint8_t discovery_finalist_index;
    uint8_t discovery_finalist_count;
    uint8_t profile_fallbacks_used;
    uint32_t evaluated_candidate_count;
    uint32_t attempted_measurement_count;
    Max35103Status last_driver_status;
} Max35103AutoCalProgress;

/** Public instance type so it can be statically allocated in AppComposition. */
typedef struct
{
    Max35103AutoCalBackend backend;
    Max35103AutoCalConfig config;
    Max35103Profile seed_profile;
    Max35103Profile stage_base_profile;
    Max35103Profile candidate_profile;
    Max35103Profile stage_best_profile;
    Max35103Profile selected_profile;
    Max35103Profile discovery_finalist_profiles[MAX35103_AUTOCAL_DISCOVERY_FINALISTS];

    Max35103AutoCalSample *samples;
    uint16_t sample_capacity;

    Max35103AutoCalState state;
    Max35103AutoCalStatus status;
    Max35103Status last_driver_status;
    uint32_t candidate_index;
    uint32_t candidate_count;
    uint16_t sample_index;
    uint16_t sample_target;
    uint32_t evaluated_candidate_count;
    uint32_t attempted_measurement_count;

    Max35103AutoCalMetrics candidate_metrics;
    Max35103AutoCalMetrics stage_best_metrics;
    Max35103AutoCalMetrics discovery_finalist_metrics[MAX35103_AUTOCAL_DISCOVERY_FINALISTS];
    Max35103Profile stage_closest_profile;
    Max35103AutoCalMetrics stage_closest_metrics;
    bool stage_best_valid;
    bool stage_closest_valid;
    bool candidate_started;
    bool candidate_configured;
    bool recovery_required;
    bool reset_performed;
    uint8_t consecutive_driver_errors;
    uint16_t busy_poll_count;
    uint8_t last_wvr_up_t1_t2_q7;
    uint8_t last_wvr_up_t2_ideal_q7;
    uint8_t last_wvr_down_t1_t2_q7;
    uint8_t last_wvr_down_t2_ideal_q7;
    uint8_t stage_retry_count;
    uint8_t discovery_finalist_count;
    uint8_t discovery_finalist_index;
    uint8_t profile_fallbacks_used;
    uint16_t perturbation_tested;
    uint16_t perturbation_passed;

    Max35103AutoCalState failure_state;
    Max35103Profile failure_profile;
    Max35103AutoCalMetrics failure_metrics;
    uint32_t failure_candidate_index;
    uint8_t failure_retry_count;

    Max35103AutoCalReport report;
    bool report_available;
} Max35103AutoCalibrator;

/**
 * Fill a conservative water-path configuration.
 *
 * For path=15000 um and transducer=1000000 Hz this produces a wave-zero
 * physical window of approximately 8.375..11.714 us, scans DPL=1..2 and
 * CT=0..3, and keeps DLY before the earliest accepted direct arrival.
 */
Max35103AutoCalStatus MAX35103_AutoCalDefaultConfig(Max35103AutoCalConfig *config,
                                                    uint32_t acoustic_path_length_um,
                                                    uint32_t transducer_frequency_hz);

/** Bind a normal Max35103Driver to the portable backend interface. */
Max35103AutoCalStatus MAX35103_AutoCalBindDriver(Max35103Driver *driver,
                                                 Max35103AutoCalBackend *backend);

/** Initialize an auto-calibrator instance and bind caller-owned sample storage. */
Max35103AutoCalStatus MAX35103_AutoCalInit(Max35103AutoCalibrator *calibrator,
                                           const Max35103AutoCalBackend *backend,
                                           const Max35103AutoCalConfig *config,
                                           const Max35103Profile *seed_profile,
                                           Max35103AutoCalSample *sample_workspace,
                                           uint16_t sample_capacity);

/** Start a new search using the configuration supplied at initialization. */
Max35103AutoCalStatus MAX35103_AutoCalStart(Max35103AutoCalibrator *calibrator);

/** Execute at most one TOF measurement or one stage-transition action. */
Max35103AutoCalStatus MAX35103_AutoCalStep(Max35103AutoCalibrator *calibrator);

/** Request cancellation of an active search. */
void MAX35103_AutoCalCancel(Max35103AutoCalibrator *calibrator);

/** Return the current search state. */
Max35103AutoCalState MAX35103_AutoCalGetState(const Max35103AutoCalibrator *calibrator);

/** Copy the current progress snapshot to caller-owned storage. */
void MAX35103_AutoCalGetProgress(const Max35103AutoCalibrator *calibrator,
                                 Max35103AutoCalProgress *progress);

/** Return true when a complete report is available. */
bool MAX35103_AutoCalHasReport(const Max35103AutoCalibrator *calibrator);

/** Copy the completed report to caller-owned storage. */
Max35103AutoCalStatus MAX35103_AutoCalGetReport(const Max35103AutoCalibrator *calibrator,
                                                Max35103AutoCalReport *report);

/** Return a stable diagnostic name for an auto-calibration state. */
const char *MAX35103_AutoCalStateName(Max35103AutoCalState state);

/** CRC-32/ISO-HDLC over explicitly encoded evidence fields. */
uint32_t MAX35103_AutoCalReportCrc32(const Max35103AutoCalReport *report);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_MAX35103_AUTOCAL_H */
