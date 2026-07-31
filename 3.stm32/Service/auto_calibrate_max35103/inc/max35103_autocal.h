/**
 ******************************************************************************
 * @file    max35103_autocal.h
 * @brief   Portable MAX35103 acoustic-profile auto-tuning service
 ******************************************************************************
 *
 * @details
 * This module searches the volatile MAX35103 configuration space for a stable
 * acoustic receive profile and produces independent verification evidence.
 * It sits above the normal MAX35103 driver and contains no STM32 HAL calls,
 * GPIO mapping, UART logging, dynamic allocation, or persistent-storage policy.
 *
 * A @e candidate is one temporary Max35103Profile generated within a search
 * stage.  The service configures the candidate, acquires a bounded measurement
 * batch, derives robust statistics, evaluates validation gates, and assigns a
 * lower-is-better score.  A @e profile is the complete MAX35103 configuration
 * selected from those candidates and carried into the next stage.
 *
 * The normal state sequence is:
 *
 * @code
 * DISCOVERY -> BIAS_CHARGE -> DLY_FINE
 *           -> OFFSET_UP_COARSE -> OFFSET_UP_FINE
 *           -> OFFSET_DOWN_COARSE -> OFFSET_DOWN_FINE
 *           -> WAVE_SELECT -> RETURN_UP -> RETURN_DOWN
 *           -> VERIFY -> ROBUSTNESS -> RESET_VERIFY -> COMPLETE
 * @endcode
 *
 * DISCOVERY performs the broad launch/DLY search and retains up to four
 * distinct launch-family finalists.  The intermediate stages tune one logical
 * parameter group at a time.  VERIFY acquires a longer independent batch,
 * ROBUSTNESS tests bounded neighboring configurations, and RESET_VERIFY proves
 * that the selected volatile profile still works after a device reset and
 * reconfiguration.  A later-stage failure may backtrack to the next discovery
 * finalist, subject to the configured fallback budget.
 *
 * Validation deliberately separates:
 *
 * - communication rate: completed, driver-valid measurements;
 * - physical rate: wave-zero UP and DOWN arrival within the expected path
 *   window;
 * - direction coherence: UP and DOWN do not lock to different acoustic cycles;
 * - waveform evidence: HIT spacing matches the launch period and WVR ratios
 *   are acceptable;
 * - statistics: median absolute deviation and cycle-slip rate are bounded.
 *
 * Time-domain fields with the @c _ps suffix are signed picoseconds.  Rates with
 * the @c _per_mille suffix use 0..1000 instead of percent so thresholds retain
 * one decimal percentage-point resolution without floating point.  WVR fields
 * with the @c _q7 suffix are raw unsigned Q1.7 ratios: 128 represents 1.0.
 * DLY values remain MAX35103 register ticks; one tick is 250 ns.
 *
 * This module does not convert TOF to flow and does not write MAX35103
 * configuration flash.  Candidate profiles are applied only to volatile
 * registers through the injected backend.
 *
 * One call to MAX35103_AutoCalStep() evaluates at most one measurement. The
 * caller therefore retains control of watchdog feeding, cancellation, logging,
 * and power policy. The injected backend also makes the search logic host
 * testable without STM32 HAL.
 *
 * @par Ownership and concurrency
 * MAX35103_AutoCalInit() copies the backend, configuration, and seed profile,
 * but borrows the caller-owned sample workspace for the lifetime of the
 * calibrator.  The service owns no memory dynamically.  A calibrator and its
 * bound driver must be advanced from one serialized foreground/task context;
 * none of the APIs are ISR-safe or internally synchronized.
 *
 * @note A successful report is evidence for the exact acoustic path, analogue
 *       front end, transducers, mounting, fluid condition, and policy used
 *       during calibration.  It is not a universal factory profile.
 * @warning Setting @c zero_flow_confirmed asserts an external physical fact.
 *          The algorithm cannot determine by itself whether the pipe truly has
 *          zero flow.
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

/** @name Persistent report identification
 * @{ */
/** ASCII-like marker used to reject unrelated or uninitialized report data. */
#define MAX35103_AUTOCAL_REPORT_MAGIC UINT32_C(0x4D43414C)
/** Schema version of the explicitly serialized evidence fields. */
#define MAX35103_AUTOCAL_REPORT_VERSION 8U
/** @} */

/** @name Fixed algorithm dimensions
 * @{ */
/** Number of neighboring-profile checks performed by ROBUSTNESS. */
#define MAX35103_AUTOCAL_PERTURBATION_COUNT 8U
/** Maximum number of distinct launch families retained after DISCOVERY. */
#define MAX35103_AUTOCAL_DISCOVERY_FINALISTS 4U
/** Sample capacity used by the optional blocking convenience wrapper. */
#define MAX35103_AUTOCAL_SAMPLE_WORKSPACE_SIZE 128U
/** @} */

/**
 * @brief Built-in volatile seed profile for the blocking convenience API.
 *
 * The macro expands to a Max35103Profile initializer.  Applications with
 * hardware-characterized settings should normally provide their own seed.
 * No field in this initializer authorizes a configuration-flash write.
 */
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
    /** Instance is initialized but no search is active. */
    MAX35103_AUTOCAL_STATE_IDLE = 0,
    /** Broad grid over seed, DPL, pulse count, CT, polarity, and coarse DLY. */
    MAX35103_AUTOCAL_STATE_DISCOVERY,
    /** Re-evaluate enabled CT values while holding the selected launch family. */
    MAX35103_AUTOCAL_STATE_BIAS_CHARGE,
    /** Search DLY around the coarse discovery winner at fine resolution. */
    MAX35103_AUTOCAL_STATE_DLY_FINE,
    /** Coarse search of the UP initial comparator offset in TOF6. */
    MAX35103_AUTOCAL_STATE_OFFSET_UP_COARSE,
    /** Fine search around the winning UP initial comparator offset. */
    MAX35103_AUTOCAL_STATE_OFFSET_UP_FINE,
    /** Coarse search of the DOWN initial comparator offset in TOF7. */
    MAX35103_AUTOCAL_STATE_OFFSET_DOWN_COARSE,
    /** Fine search around the winning DOWN initial comparator offset. */
    MAX35103_AUTOCAL_STATE_OFFSET_DOWN_FINE,
    /** Search T2WV and configured HIT count/sequence using longer batches. */
    MAX35103_AUTOCAL_STATE_WAVE_SELECT,
    /** Search the signed UP comparator return offset in TOF6. */
    MAX35103_AUTOCAL_STATE_RETURN_UP,
    /** Search the signed DOWN comparator return offset in TOF7. */
    MAX35103_AUTOCAL_STATE_RETURN_DOWN,
    /** Acquire a long independent batch for the selected profile. */
    MAX35103_AUTOCAL_STATE_VERIFY,
    /** Test bounded DLY, UP/DOWN initial-offset, and T2WV perturbations. */
    MAX35103_AUTOCAL_STATE_ROBUSTNESS,
    /** Reset, reapply, and independently verify the profile once more. */
    MAX35103_AUTOCAL_STATE_RESET_VERIFY,
    /** Terminal success; a CRC-protected evidence report is available. */
    MAX35103_AUTOCAL_STATE_COMPLETE,
    /** Terminal failure after retry/fallback or driver-error exhaustion. */
    MAX35103_AUTOCAL_STATE_FAILED,
    /** Terminal state entered by an explicit cancellation request. */
    MAX35103_AUTOCAL_STATE_CANCELLED,
} Max35103AutoCalState;

/**
 * @brief Status values returned by the auto-calibration service.
 */
typedef enum
{
    /** Operation succeeded; no active search is implied. */
    MAX35103_AUTOCAL_OK = 0,
    /** Search is active and requires further MAX35103_AutoCalStep() calls. */
    MAX35103_AUTOCAL_RUNNING = 1,
    /** Search completed and a verified report can be retrieved. */
    MAX35103_AUTOCAL_COMPLETE = 2,
    /** A pointer, range, profile, callback, or workspace contract is invalid. */
    MAX35103_AUTOCAL_INVALID_ARG = -1,
    /** The backend/driver could not be recovered within its configured budget. */
    MAX35103_AUTOCAL_DRIVER_ERROR = -2,
    /** No candidate survived all required gates, retries, and fallbacks. */
    MAX35103_AUTOCAL_NO_CANDIDATE = -3,
    /** Search ended because MAX35103_AutoCalCancel() was requested. */
    MAX35103_AUTOCAL_CANCELLED = -4,
} Max35103AutoCalStatus;

/**
 * @brief Confidence level assigned to a completed calibration report.
 */
typedef enum
{
    /** No completed or trustworthy calibration evidence exists. */
    MAX35103_AUTOCAL_CONFIDENCE_NONE = 0,
    /** A candidate exists but has not completed independent acoustic checks. */
    MAX35103_AUTOCAL_CONFIDENCE_CANDIDATE,
    /** Acoustic, robustness, and post-reset verification all passed. */
    MAX35103_AUTOCAL_CONFIDENCE_ACOUSTIC_VERIFIED,
    /** Acoustic verification passed during an externally confirmed zero-flow condition. */
    MAX35103_AUTOCAL_CONFIDENCE_ZERO_FLOW_COMPENSATED,
} Max35103AutoCalConfidence;

/**
 * @brief Hardware-independent operations required by the search engine.
 *
 * @details
 * These callbacks form the only boundary between the portable algorithm and
 * the MAX35103 driver/platform.  All callbacks run synchronously in the same
 * foreground context that called MAX35103_AutoCalStep().
 *
 * configure() must apply @p profile to volatile registers and verify readback.
 * It must not persist a candidate to configuration flash.
 *
 * measure() must execute one TOF measurement and return the averaged result
 * together with per-HIT wave evidence belonging to that same configured
 * profile/measurement.  A completed but invalid device result must be consumed
 * so stale mailbox data cannot leak into the next candidate.
 *
 * reset() must restore the device/driver to a state in which configure() may be
 * called again.  The search engine deliberately marks the candidate
 * unconfigured after reset.
 *
 * Callback pointers and @c context are copied into Max35103AutoCalibrator;
 * objects referenced by @c context remain owned by the caller.
 */
typedef struct
{
    /**
     * Apply and read back one volatile candidate profile.
     *
     * @param[in] context Backend-defined object.
     * @param[in] profile Complete candidate profile; valid for this call only.
     * @return A Max35103Status value from the driver/backend.
     */
    Max35103Status (*configure)(void *context, const Max35103Profile *profile);

    /**
     * Acquire one averaged TOF result plus matching receive-wave evidence.
     *
     * @param[in]  context Backend-defined object.
     * @param[out] result  Averaged/raw TOF result for this attempt.
     * @param[out] wave    Per-HIT timing and WVR evidence for this attempt.
     * @return MAX35103_OK only when both output objects contain usable data.
     */
    Max35103Status (*measure)(void *context, Max35103RawResult *result, Max35103WaveEvidence *wave);

    /**
     * Reset the device and driver state before recovery or RESET_VERIFY.
     *
     * @param[in] context Backend-defined object.
     * @return A Max35103Status value from the reset operation.
     */
    Max35103Status (*reset)(void *context);

    /** Opaque pointer passed unchanged to every callback; owned by the caller. */
    void *context;
} Max35103AutoCalBackend;

/**
 * @brief Search ranges, statistical gates, and retry policy.
 *
 * @details
 * Every minimum/maximum range is inclusive.  CT mask bit @c n enables CT=@c n
 * in DISCOVERY and BIAS_CHARGE.  The initial comparator offset is the unsigned
 * 7-bit low field of TOF6/TOF7.  The return offset is the signed high byte.
 *
 * @c samples_per_candidate, @c finalist_samples, and
 * @c verification_samples must not exceed the sample workspace capacity
 * supplied to MAX35103_AutoCalInit().
 */
typedef struct
{
    /** One-way acoustic propagation distance, in micrometres. */
    uint32_t acoustic_path_length_um;

    /*
     * Accepted wave-zero arrival window. The service removes the programmed
     * HIT wave-number delay before applying this gate. Board-specific fixed
     * analog/circuit delay may still be included by adjusting this window.
     */
    /** Inclusive minimum accepted wave-zero UP/DOWN arrival, in picoseconds. */
    int64_t expected_min_tof_ps;
    /** Inclusive maximum accepted wave-zero UP/DOWN arrival, in picoseconds. */
    int64_t expected_max_tof_ps;

    /** Minimum DPL field value searched in DISCOVERY; valid range is 1..15. */
    uint8_t dpl_min;
    /** Maximum DPL field value searched in DISCOVERY; valid range is 1..15. */
    uint8_t dpl_max;
    /** Minimum launch pulse count searched in DISCOVERY. */
    uint8_t pulse_count_min;
    /** Maximum launch pulse count searched in DISCOVERY. */
    uint8_t pulse_count_max;
    /** Inclusive DISCOVERY increment for the launch pulse count. */
    uint8_t pulse_count_step;
    /** Low four bits enable CT values 0..3; at least one bit must be set. */
    uint8_t ct_mask;
    /** True to evaluate both STOP_POL values; false to preserve seed polarity. */
    bool try_both_polarities;

    /** Minimum MAX35103 DLY register value, in 250 ns ticks. */
    uint16_t dly_min;
    /** Maximum MAX35103 DLY register value, in 250 ns ticks. */
    uint16_t dly_max;
    /** DLY increment used by the broad DISCOVERY grid, in 250 ns ticks. */
    uint16_t dly_coarse_step;
    /** DLY increment used by DLY_FINE and robustness, in 250 ns ticks. */
    uint16_t dly_fine_step;

    /** Minimum unsigned initial comparator offset searched for UP and DOWN. */
    uint8_t initial_offset_min;
    /** Maximum unsigned initial comparator offset; must not exceed 127. */
    uint8_t initial_offset_max;
    /** Initial-offset increment used by each coarse search. */
    uint8_t initial_offset_coarse_step;
    /** Initial-offset increment used by each fine search and robustness. */
    uint8_t initial_offset_fine_step;
    /** Minimum signed comparator return offset searched for each direction. */
    int8_t return_offset_min;
    /** Maximum signed comparator return offset searched for each direction. */
    int8_t return_offset_max;
    /** Return-offset increment interpreted in signed-register LSBs. */
    uint8_t return_offset_step;

    /** Minimum T2WV receive-wave number; the implementation requires >= 2. */
    uint8_t t2_wave_min;
    /** Maximum T2WV receive-wave number; the implementation requires <= 57. */
    uint8_t t2_wave_max;
    /** Minimum number of HIT timestamps included in TOF averaging. */
    uint8_t hit_count_min;
    /** Maximum HIT count; must not exceed MAX35103_WAVE_HIT_COUNT. */
    uint8_t hit_count_max;

    /** Measurement attempts per ordinary discovery/tuning candidate. */
    uint16_t samples_per_candidate;
    /* Longer batches used by WAVE_SELECT and comparator-return tuning. */
    /** Attempts per WAVE_SELECT and RETURN_UP/DOWN candidate. */
    uint16_t finalist_samples;
    /** Attempts used by VERIFY and RESET_VERIFY. */
    uint16_t verification_samples;
    /** Minimum driver-valid attempt rate, expressed in 0..1000 per mille. */
    uint16_t min_valid_rate_per_mille;
    /* Used by BIAS/DLY/OFFSET/WAVE/RETURN search stages. */
    /** Minimum physical-arrival rate for intermediate tuning stages. */
    uint16_t min_tuning_physical_rate_per_mille;
    /* Used by Discovery, Verify, Robustness and Reset Verify. */
    /** Minimum physical-arrival rate for discovery and final evidence stages. */
    uint16_t min_physical_rate_per_mille;
    /** Minimum rate of samples with usable per-HIT period evidence. */
    uint16_t min_wave_valid_rate_per_mille;
    /** Minimum accepted WVR success rate for the stage-relevant direction(s). */
    uint16_t min_wvr_good_rate_per_mille;
    /*
     * WVR packs two different ratios.  Keep separate lower bounds because
     * the early-edge t1 pulse can legitimately be much narrower than t2,
     * while t2/tideal must still remain representative of the launch period.
     */
    /** Inclusive lower limit for WVR t1/t2 in unsigned Q1.7. */
    uint8_t wvr_t1_t2_min_q7;
    /** Inclusive lower limit for WVR t2/tideal in unsigned Q1.7. */
    uint8_t wvr_t2_ideal_min_q7;
    /** Shared inclusive upper limit for both WVR ratios in unsigned Q1.7. */
    uint8_t wvr_ratio_max_q7;

    /** Maximum median absolute deviation of UP or DOWN arrival, in ps. */
    int64_t max_tof_mad_ps;
    /** Maximum median absolute deviation of TOF difference, in ps. */
    int64_t max_diff_mad_ps;
    /** Maximum median absolute HIT-period error, in ps. */
    int64_t max_period_error_ps;
    /*
     * Maximum median UP/DOWN separation.  This rejects profiles that lock the
     * two directions to different acoustic cycles while both remain inside
     * the broad physical arrival window.
     */
    /** Maximum absolute separation of median UP and DOWN wave-zero arrival, in ps. */
    int64_t max_direction_delta_ps;
    /** Absolute maximum cycle-slip count permitted by the statistics gate. */
    uint16_t max_cycle_slips;
    /** Maximum cycle-slip rate among valid samples, in per mille. */
    uint16_t max_cycle_slip_rate_per_mille;
    /** Minimum passing perturbations required from the fixed robustness set. */
    uint8_t required_perturbation_passes;
    /** Consecutive backend errors permitted before terminal driver failure. */
    uint8_t max_consecutive_driver_errors;
    /** Number of complete retries permitted for a failed stage. */
    uint8_t max_stage_retries;
    /** Number of later-stage fallbacks to another discovery finalist. */
    uint8_t max_profile_fallbacks;
    /** Maximum consecutive MAX35103_BUSY polls for one configure/measure action. */
    uint16_t max_busy_polls;

    /**
     * External assertion that calibration was performed at true zero flow.
     *
     * This flag only upgrades report confidence and permits the measured median
     * TOF difference to be treated as zero-flow offset evidence.
     */
    bool zero_flow_confirmed;
} Max35103AutoCalConfig;

/**
 * @brief One measurement attempt stored in the caller-owned workspace.
 *
 * @details
 * UP and DOWN are wave-zero arrival estimates: each configured HIT timestamp
 * is shifted back by its programmed wave number times the expected period,
 * then valid reconstructed arrivals are averaged.  DIFF remains the driver's
 * averaged UP-minus-DOWN result and is not reconstructed from the two medians.
 *
 * @c work_ps is scratch storage used by in-place quickselect for median and MAD
 * calculations.  Its value has no meaning after metrics are finalized.
 *
 * Use sizeof(Max35103AutoCalSample) when budgeting RAM because alignment
 * differs by compiler; a typical 32/64-bit ABI uses 48 bytes per sample
 * (6 KiB for 128 samples).
 */
typedef struct
{
    /** Reconstructed wave-zero upstream arrival, in picoseconds. */
    int64_t tof_up_ps;
    /** Reconstructed wave-zero downstream arrival, in picoseconds. */
    int64_t tof_down_ps;
    /** Device-provided averaged upstream-minus-downstream TOF, in picoseconds. */
    int64_t tof_diff_ps;
    /** Mean absolute error of adjacent HIT spacing, in picoseconds. */
    int64_t period_error_ps;
    /** Internal selection scratch value, in picoseconds. */
    int64_t work_ps;
    /** Bitwise OR of MAX35103_AUTOCAL_SAMPLE_* evidence flags. */
    uint8_t flags;
} Max35103AutoCalSample;

/** Driver result and per-wave evidence were both usable. */
#define MAX35103_AUTOCAL_SAMPLE_VALID       0x01U
/** Reconstructed UP and DOWN arrivals both passed the physical window. */
#define MAX35103_AUTOCAL_SAMPLE_PHYSICAL    0x02U
/** At least two HITs produced positive, usable period evidence. */
#define MAX35103_AUTOCAL_SAMPLE_WAVE_VALID  0x04U
/** Both upstream WVR ratios were inside their configured Q1.7 limits. */
#define MAX35103_AUTOCAL_SAMPLE_WVR_UP_GOOD 0x08U
/** Both downstream WVR ratios were inside their configured Q1.7 limits. */
#define MAX35103_AUTOCAL_SAMPLE_WVR_DN_GOOD 0x10U
/** Upstream and downstream WVR checks both passed. */
#define MAX35103_AUTOCAL_SAMPLE_WVR_GOOD    0x20U

/**
 * @brief Aggregated statistics, gate results, and score for one candidate.
 *
 * Counts and rates describe one completed candidate batch.  Medians and MADs
 * are computed only from entries carrying MAX35103_AUTOCAL_SAMPLE_VALID.
 * Period statistics are computed from the stricter subset also carrying
 * MAX35103_AUTOCAL_SAMPLE_WAVE_VALID.
 *
 * @c score is a saturated, lexicographically weighted cost: lower is better.
 * Gate failures receive large penalties, so score ranking never converts a
 * physically invalid candidate into an eligible one.
 */
typedef struct
{
    /** Total measurement attempts in the batch, including failures/timeouts. */
    uint16_t attempted_count;
    /** Attempts with valid driver result and wave evidence. */
    uint16_t valid_count;
    /** Attempts whose reconstructed UP and DOWN arrivals passed the path window. */
    uint16_t physical_count;
    /** Attempts with usable adjacent-HIT period evidence. */
    uint16_t wave_valid_count;
    /** Valid attempts whose upstream WVR pair passed. */
    uint16_t wvr_up_good_count;
    /** Valid attempts whose downstream WVR pair passed. */
    uint16_t wvr_down_good_count;
    /** Valid attempts whose upstream and downstream WVR pairs both passed. */
    uint16_t wvr_good_count;
    /** Attempts for which the backend returned MAX35103_TIMEOUT. */
    uint16_t timeout_count;
    /** Valid attempts displaced from a direction median by over half a period. */
    uint16_t cycle_slip_count;
    /** @c valid_count / @c attempted_count, rounded to 0..1000 per mille. */
    uint16_t valid_rate_per_mille;
    /** @c physical_count / @c attempted_count, rounded to per mille. */
    uint16_t physical_rate_per_mille;
    /** Stage-dependent physical-rate threshold used for this candidate. */
    uint16_t physical_rate_required_per_mille;
    /** @c wave_valid_count / @c attempted_count, rounded to per mille. */
    uint16_t wave_valid_rate_per_mille;
    /** Upstream WVR success rate among valid samples, in per mille. */
    uint16_t wvr_up_good_rate_per_mille;
    /** Downstream WVR success rate among valid samples, in per mille. */
    uint16_t wvr_down_good_rate_per_mille;
    /** Bidirectional WVR success rate among valid samples, in per mille. */
    uint16_t wvr_good_rate_per_mille;
    /** @c cycle_slip_count / @c valid_count, rounded to per mille. */
    uint16_t cycle_slip_rate_per_mille;

    /** Median reconstructed upstream wave-zero arrival, in picoseconds. */
    int64_t median_tof_up_ps;
    /** Median reconstructed downstream wave-zero arrival, in picoseconds. */
    int64_t median_tof_down_ps;
    /** Median device-provided TOF difference, in picoseconds. */
    int64_t median_tof_diff_ps;
    /** Absolute UP/DOWN median separation, in picoseconds. */
    int64_t direction_delta_ps;
    /** Median absolute deviation of upstream arrival, in picoseconds. */
    int64_t mad_tof_up_ps;
    /** Median absolute deviation of downstream arrival, in picoseconds. */
    int64_t mad_tof_down_ps;
    /** Median absolute deviation of TOF difference, in picoseconds. */
    int64_t mad_tof_diff_ps;
    /** Median adjacent-HIT period error, in picoseconds. */
    int64_t median_period_error_ps;
    /** MAD of adjacent-HIT period error, in picoseconds. */
    int64_t mad_period_error_ps;

    /** Saturated lower-is-better ranking cost used within a search stage. */
    uint64_t score;
    /** Valid-measurement rate meets @c min_valid_rate_per_mille. */
    bool communication_gate;
    /** Median UP/DOWN separation meets @c max_direction_delta_ps. */
    bool direction_gate;
    /** Physical rate, direction coherence, and both arrival medians pass. */
    bool physical_gate;
    /** Wave evidence rate and median HIT-period error pass. */
    bool period_gate;
    /** Period gate and bidirectional WVR success rate pass. */
    bool waveform_gate;
    /** Stage-specific period/WVR requirements pass. */
    bool stage_waveform_gate;
    /** MAD and cycle-slip count/rate limits pass. */
    bool statistics_gate;
    /** Full final-policy conjunction used by VERIFY and ROBUSTNESS. */
    bool passed;
} Max35103AutoCalMetrics;

/**
 * @brief Persistent evidence report produced by a completed calibration.
 *
 * @details
 * The report contains only value types and can be stored by the application.
 * @c evidence_crc32 covers explicitly encoded semantic fields rather than the
 * raw structure bytes, so compiler padding and native endianness do not affect
 * the checksum.  Validate @c magic, @c report_version, @c report_size, and the
 * recomputed CRC before accepting a restored report.
 */
typedef struct
{
    /** Must equal MAX35103_AUTOCAL_REPORT_MAGIC. */
    uint32_t magic;
    /** Must equal MAX35103_AUTOCAL_REPORT_VERSION for this schema. */
    uint16_t report_version;
    /** Producer's sizeof(Max35103AutoCalReport), for compatibility checks. */
    uint16_t report_size;
    /** One-way acoustic path used to derive the expected arrival window, in um. */
    uint32_t acoustic_path_length_um;
    /** Inclusive minimum accepted wave-zero arrival, in picoseconds. */
    int64_t expected_min_tof_ps;
    /** Inclusive maximum accepted wave-zero arrival, in picoseconds. */
    int64_t expected_max_tof_ps;

    /** Complete volatile MAX35103 profile that passed post-reset verification. */
    Max35103Profile selected_profile;
    /** Metrics from the final RESET_VERIFY batch. */
    Max35103AutoCalMetrics verification;

    /** Total candidate batches evaluated across stages, retries, and fallbacks. */
    uint32_t evaluated_candidate_count;
    /** Total measurement attempts issued across the complete search. */
    uint32_t attempted_measurement_count;
    /** Number of realizable robustness perturbations actually tested. */
    uint16_t perturbation_tested;
    /** Number of tested perturbations that passed the full gate set. */
    uint16_t perturbation_passed;
    /** Number of times tuning restarted from another discovery finalist. */
    uint8_t profile_fallbacks_used;
    /** Final median TOF difference at externally confirmed zero flow, in ps. */
    int64_t zero_flow_offset_ps;
    /** MAD associated with @c zero_flow_offset_ps, in ps. */
    int64_t zero_flow_mad_ps;
    /** Evidence grade assigned when the report was finalized. */
    Max35103AutoCalConfidence confidence;
    /** True only after reset, reconfiguration, and RESET_VERIFY succeeded. */
    bool reset_verified;
    /** CRC-32/ISO-HDLC over the report's explicitly encoded evidence fields. */
    uint32_t evidence_crc32;
} Max35103AutoCalReport;

/**
 * @brief Read-only progress snapshot for diagnostics and user interfaces.
 *
 * The snapshot contains copies only; it does not expose or transfer ownership
 * of the calibrator's workspace.  Indices are zero-based.  During active
 * acquisition, @c sample_index is the number of attempts already recorded for
 * the current candidate.
 */
typedef struct
{
    /** Current search or terminal state. */
    Max35103AutoCalState state;
    /** Zero-based candidate index within the current stage. */
    uint32_t candidate_index;
    /** Total candidate slots in the current stage. */
    uint32_t candidate_count;
    /** Measurement attempts already recorded for the current candidate. */
    uint16_t sample_index;
    /** Measurement attempts required before the candidate is evaluated. */
    uint16_t sample_target;
    /** Number of complete restarts already used for the current stage. */
    uint8_t stage_retry_count;
    /** Zero-based discovery finalist currently driving later-stage tuning. */
    uint8_t discovery_finalist_index;
    /** Number of retained discovery finalists. */
    uint8_t discovery_finalist_count;
    /** Number of later-stage profile fallbacks already used. */
    uint8_t profile_fallbacks_used;
    /** Cumulative number of candidate batches evaluated. */
    uint32_t evaluated_candidate_count;
    /** Cumulative number of measurement attempts issued. */
    uint32_t attempted_measurement_count;
    /** Most recent status returned by configure(), measure(), or reset(). */
    Max35103Status last_driver_status;
} Max35103AutoCalProgress;

/**
 * @brief Complete runtime state of one auto-calibration session.
 *
 * @details
 * The type is public so embedded applications can allocate it statically and
 * avoid a heap.  Its members are observable for diagnostics, but application
 * code should modify state only through the public API.
 *
 * Profile roles:
 *
 * - @c seed_profile: immutable copy of the caller's starting point;
 * - @c stage_base_profile: selected profile frozen on stage entry;
 * - @c candidate_profile: temporary profile currently configured/evaluated;
 * - @c stage_best_profile: lowest-score eligible profile in the stage;
 * - @c selected_profile: committed result carried between stages.
 *
 * The @c samples pointer is borrowed caller storage.  All other aggregates are
 * stored by value in this object.
 */
typedef struct
{
    /** Copied hardware-independent backend and opaque driver context. */
    Max35103AutoCalBackend backend;
    /** Copied search ranges, gates, and retry policy. */
    Max35103AutoCalConfig config;
    /** Immutable starting profile copied during initialization. */
    Max35103Profile seed_profile;
    /** Profile snapshot from which candidates in the current stage are derived. */
    Max35103Profile stage_base_profile;
    /** Candidate currently being configured, measured, or most recently scored. */
    Max35103Profile candidate_profile;
    /** Lowest-score eligible profile found in the current stage. */
    Max35103Profile stage_best_profile;
    /** Profile committed by the previous successful stage. */
    Max35103Profile selected_profile;
    /** Ordered, lower-score-first finalists retained by DISCOVERY. */
    Max35103Profile discovery_finalist_profiles[MAX35103_AUTOCAL_DISCOVERY_FINALISTS];

    /** Borrowed measurement workspace; valid for the full instance lifetime. */
    Max35103AutoCalSample *samples;
    /** Number of entries available through @c samples. */
    uint16_t sample_capacity;

    /** Current state-machine state. */
    Max35103AutoCalState state;
    /** Last public service status. */
    Max35103AutoCalStatus status;
    /** Most recent backend/driver status for diagnostics. */
    Max35103Status last_driver_status;
    /** Zero-based candidate position within the current stage. */
    uint32_t candidate_index;
    /** Number of candidate slots in the current stage. */
    uint32_t candidate_count;
    /** Number of attempts already stored for the active candidate. */
    uint16_t sample_index;
    /** Required attempts for the active candidate's batch. */
    uint16_t sample_target;
    /** Cumulative evaluated-candidate counter for the whole run. */
    uint32_t evaluated_candidate_count;
    /** Cumulative measurement-attempt counter for the whole run. */
    uint32_t attempted_measurement_count;

    /** Metrics of the active or most recently completed candidate. */
    Max35103AutoCalMetrics candidate_metrics;
    /** Metrics paired with @c stage_best_profile. */
    Max35103AutoCalMetrics stage_best_metrics;
    /** Metrics paired element-for-element with discovery finalist profiles. */
    Max35103AutoCalMetrics discovery_finalist_metrics[MAX35103_AUTOCAL_DISCOVERY_FINALISTS];
    /** Lowest-score candidate seen, even if it failed eligibility gates. */
    Max35103Profile stage_closest_profile;
    /** Metrics paired with @c stage_closest_profile for failure diagnostics. */
    Max35103AutoCalMetrics stage_closest_metrics;
    /** True after at least one eligible candidate has become the stage winner. */
    bool stage_best_valid;
    /** True after at least one candidate has been scored in the stage. */
    bool stage_closest_valid;
    /** True after per-candidate counters have been initialized. */
    bool candidate_started;
    /** True while @c candidate_profile is applied to the device. */
    bool candidate_configured;
    /** Requests backend reset before the next configure/measure action. */
    bool recovery_required;
    /** Ensures RESET_VERIFY performs exactly one reset before its batch. */
    bool reset_performed;
    /** Consecutive recoverable backend errors; cleared by a successful operation. */
    uint8_t consecutive_driver_errors;
    /** Consecutive MAX35103_BUSY results for the pending action. */
    uint16_t busy_poll_count;
    /** Most recently observed upstream WVR t1/t2, raw unsigned Q1.7. */
    uint8_t last_wvr_up_t1_t2_q7;
    /** Most recently observed upstream WVR t2/tideal, raw unsigned Q1.7. */
    uint8_t last_wvr_up_t2_ideal_q7;
    /** Most recently observed downstream WVR t1/t2, raw unsigned Q1.7. */
    uint8_t last_wvr_down_t1_t2_q7;
    /** Most recently observed downstream WVR t2/tideal, raw unsigned Q1.7. */
    uint8_t last_wvr_down_t2_ideal_q7;
    /** Number of full retries already used by the current stage. */
    uint8_t stage_retry_count;
    /** Number of populated entries in the discovery finalist arrays. */
    uint8_t discovery_finalist_count;
    /** Finalist currently used as the root for later tuning stages. */
    uint8_t discovery_finalist_index;
    /** Number of completed fallbacks to a later discovery finalist. */
    uint8_t profile_fallbacks_used;
    /** Number of realizable robustness perturbations evaluated. */
    uint16_t perturbation_tested;
    /** Number of evaluated robustness perturbations passing all final gates. */
    uint16_t perturbation_passed;

    /** State in which the terminal failure was captured. */
    Max35103AutoCalState failure_state;
    /** Closest or active candidate retained when failure was captured. */
    Max35103Profile failure_profile;
    /** Metrics paired with @c failure_profile. */
    Max35103AutoCalMetrics failure_metrics;
    /** Stage-local candidate index captured at failure. */
    uint32_t failure_candidate_index;
    /** Stage retry count captured at failure. */
    uint8_t failure_retry_count;

    /** Completed evidence report, valid only when @c report_available is true. */
    Max35103AutoCalReport report;
    /** Publication flag set only after successful RESET_VERIFY finalization. */
    bool report_available;
} Max35103AutoCalibrator;

/**
 * @brief Fill a conservative default policy for a water acoustic path.
 *
 * The function derives the physical arrival window using an assumed water
 * sound-speed range of 1400..1600 m/s plus timing margin.  It estimates the
 * nominal DPL from the transducer frequency, then fills all search ranges,
 * batch sizes, gates, robustness requirements, and recovery budgets.
 *
 * For @p acoustic_path_length_um = 15000 and
 * @p transducer_frequency_hz = 1000000 this produces a wave-zero physical
 * window of approximately 8.375..11.714 us, scans DPL=1..2 and CT=0..3, and
 * keeps DLY before the earliest accepted direct arrival.
 *
 * @param[out] config
 *     Destination overwritten with a complete configuration.
 * @param[in] acoustic_path_length_um
 *     One-way transducer acoustic path, in micrometres.
 * @param[in] transducer_frequency_hz
 *     Nominal transducer center frequency, in hertz.
 *
 * @return MAX35103_AUTOCAL_OK on success.
 * @return MAX35103_AUTOCAL_INVALID_ARG if any pointer or physical input is
 *         zero/invalid.
 *
 * @note The generated values are conservative starting policy, not a
 *       substitute for product-level limits derived from the actual pipe,
 *       analogue front end, transducer bandwidth, fluid, and temperature.
 */
Max35103AutoCalStatus MAX35103_AutoCalDefaultConfig(Max35103AutoCalConfig *config,
                                                    uint32_t acoustic_path_length_um,
                                                    uint32_t transducer_frequency_hz);

/**
 * @brief Bind a normal Max35103Driver to the portable backend interface.
 *
 * The resulting backend forwards configure, measurement, wave-evidence, and
 * reset operations to the supplied driver.  The driver pointer is borrowed and
 * stored as backend context.
 *
 * @param[in,out] driver
 *     Initialized driver instance that remains valid for the calibration run.
 * @param[out] backend
 *     Destination overwritten with all required callbacks and @p driver as
 *     opaque context.
 *
 * @return MAX35103_AUTOCAL_OK on success.
 * @return MAX35103_AUTOCAL_INVALID_ARG if either pointer is NULL.
 *
 * @warning Calls made through this backend are synchronous and must use the
 *          foreground context that owns the MAX35103 driver.
 */
Max35103AutoCalStatus MAX35103_AutoCalBindDriver(Max35103Driver *driver,
                                                 Max35103AutoCalBackend *backend);

/**
 * @brief Initialize a calibrator and bind caller-owned sample storage.
 *
 * The function validates all callbacks, ranges, thresholds, workspace sizes,
 * and the seed profile.  It then clears @p calibrator and copies @p backend,
 * @p config, and @p seed_profile into the instance.  It does not access
 * hardware and does not start the state machine.
 *
 * @param[out] calibrator
 *     Instance initialized to MAX35103_AUTOCAL_STATE_IDLE.
 * @param[in] backend
 *     Complete callback table; copied by value.
 * @param[in] config
 *     Search policy; copied by value.
 * @param[in] seed_profile
 *     Known-safe profile; copied by value and validated.
 * @param[in,out] sample_workspace
 *     Caller-owned array used for acquisition and in-place robust statistics.
 * @param[in] sample_capacity
 *     Number of Max35103AutoCalSample entries in @p sample_workspace.
 *
 * @return MAX35103_AUTOCAL_OK on successful initialization.
 * @return MAX35103_AUTOCAL_INVALID_ARG when any contract is invalid or the
 *         workspace cannot contain the largest configured batch.
 *
 * @pre @p sample_workspace remains allocated and exclusively available until
 *      the calibrator is no longer used.
 */
Max35103AutoCalStatus MAX35103_AutoCalInit(Max35103AutoCalibrator *calibrator,
                                           const Max35103AutoCalBackend *backend,
                                           const Max35103AutoCalConfig *config,
                                           const Max35103Profile *seed_profile,
                                           Max35103AutoCalSample *sample_workspace,
                                           uint16_t sample_capacity);

/**
 * @brief Reset runtime evidence and start a new search.
 *
 * A call from IDLE, COMPLETE, FAILED, or CANCELLED clears counters, finalists,
 * failure evidence, and any old report, then enters DISCOVERY using the
 * original copied seed profile.  Calling while another search is active does
 * not restart it.
 *
 * @param[in,out] calibrator Initialized instance.
 *
 * @return MAX35103_AUTOCAL_RUNNING when the search is active, including when
 *         it was already active.
 * @return MAX35103_AUTOCAL_INVALID_ARG if instance contracts are no longer
 *         valid or DISCOVERY has no candidates.
 *
 * @warning A new search invalidates any previously available report.
 */
Max35103AutoCalStatus MAX35103_AutoCalStart(Max35103AutoCalibrator *calibrator);

/**
 * @brief Advance the state machine by one bounded action.
 *
 * One call performs at most one of the following: backend reset, candidate
 * generation/configuration, one measurement attempt, candidate finalization,
 * or stage transition.  This bounded-work contract permits cooperative
 * scheduling and watchdog servicing by the caller.
 *
 * MAX35103_BUSY does not consume a measurement sample.  Completed timeout or
 * invalid-result attempts do consume a sample slot so a bad candidate cannot
 * block the search indefinitely.  Recoverable transport/device failures cause
 * a reset before the candidate is configured again.
 *
 * @param[in,out] calibrator Active initialized instance.
 *
 * @return MAX35103_AUTOCAL_RUNNING while further steps are required.
 * @return MAX35103_AUTOCAL_COMPLETE after RESET_VERIFY and report finalization.
 * @return MAX35103_AUTOCAL_OK while the instance is IDLE.
 * @return MAX35103_AUTOCAL_INVALID_ARG if @p calibrator is NULL.
 * @return A retained negative terminal status in FAILED or CANCELLED.
 *
 * @warning Not ISR-safe and not reentrant.  Serialize access with the bound
 *          driver and do not mutate public instance fields concurrently.
 */
Max35103AutoCalStatus MAX35103_AutoCalStep(Max35103AutoCalibrator *calibrator);

/**
 * @brief Request immediate cancellation of a nonterminal search.
 *
 * @param[in,out] calibrator Instance to cancel; NULL is ignored.
 *
 * @post A cancellable instance enters MAX35103_AUTOCAL_STATE_CANCELLED and no
 *       report is published.
 * @note COMPLETE and FAILED instances are left unchanged.
 * @note This operation changes software state only; it does not reset the
 *       device or restore the seed/selected profile.
 */
void MAX35103_AutoCalCancel(Max35103AutoCalibrator *calibrator);

/**
 * @brief Return the current search state.
 *
 * @param[in] calibrator Instance to inspect.
 * @return Current state, or MAX35103_AUTOCAL_STATE_FAILED for NULL.
 */
Max35103AutoCalState MAX35103_AutoCalGetState(const Max35103AutoCalibrator *calibrator);

/**
 * @brief Copy a diagnostic progress snapshot to caller-owned storage.
 *
 * @param[in] calibrator Instance to inspect, or NULL for a synthetic invalid
 *            FAILED snapshot.
 * @param[out] progress Destination snapshot; NULL is ignored.
 *
 * @note This function performs plain field copies and provides no internal
 *       synchronization.  Call it from the same serialized context as Step().
 */
void MAX35103_AutoCalGetProgress(const Max35103AutoCalibrator *calibrator,
                                 Max35103AutoCalProgress *progress);

/**
 * @brief Test whether a completed evidence report is available.
 *
 * @param[in] calibrator Instance to inspect.
 * @return true only after successful RESET_VERIFY report publication.
 * @return false for NULL, active, failed, cancelled, or restarted instances.
 */
bool MAX35103_AutoCalHasReport(const Max35103AutoCalibrator *calibrator);

/**
 * @brief Copy the completed evidence report to caller-owned storage.
 *
 * @param[in] calibrator Instance containing the report.
 * @param[out] report Destination for a complete value copy.
 *
 * @return MAX35103_AUTOCAL_COMPLETE when the report was copied.
 * @return MAX35103_AUTOCAL_RUNNING when no report is currently published.
 * @return MAX35103_AUTOCAL_INVALID_ARG if either pointer is NULL.
 */
Max35103AutoCalStatus MAX35103_AutoCalGetReport(const Max35103AutoCalibrator *calibrator,
                                                Max35103AutoCalReport *report);

/**
 * @brief Return a stable diagnostic name for an auto-calibration state.
 *
 * @param[in] state State enumeration value.
 * @return Pointer to a static, NUL-terminated uppercase name.
 * @return @c "UNKNOWN" for an unrecognized value.
 *
 * @note The returned string must not be modified or freed.
 */
const char *MAX35103_AutoCalStateName(Max35103AutoCalState state);

/**
 * @brief Calculate CRC-32/ISO-HDLC over explicitly encoded evidence fields.
 *
 * Multi-byte integers are fed least-significant byte first.  Structure padding
 * is never read, and @c evidence_crc32 itself is excluded, so results are
 * stable across compiler padding and CPU endianness.
 *
 * @param[in] report Report to checksum.
 * @return Canonical CRC value, or zero when @p report is NULL.
 *
 * @note CRC detects accidental corruption; it is not a cryptographic
 *       authenticity mechanism.
 */
uint32_t MAX35103_AutoCalReportCrc32(const Max35103AutoCalReport *report);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_MAX35103_AUTOCAL_H */