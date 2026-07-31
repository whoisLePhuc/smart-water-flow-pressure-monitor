/**
 ******************************************************************************
 * @file    max35103_autocal.c
 * @brief   Portable MAX35103 acoustic-profile auto-tuning service
 ******************************************************************************
 *
 * @details
 * The implementation performs a staged, deterministic search over volatile
 * Max35103Profile values. Every stage starts from the profile committed by the
 * preceding stage and varies only its assigned parameter group. This
 * coordinate-search structure keeps the candidate count bounded while making
 * every accepted change traceable in the final report.
 *
 * For each candidate the service:
 *
 * 1. applies and verifies the complete profile through the injected backend;
 * 2. acquires a fixed-size batch of averaged TOF and per-HIT evidence;
 * 3. normalizes programmed HIT timestamps back to estimated wave-zero arrival;
 * 4. computes rates, medians, median absolute deviations, direction mismatch,
 *    period error, WVR success, and cycle slips;
 * 5. applies stage-specific gates and a saturated lower-is-better score;
 * 6. retains the best eligible candidate and the closest diagnostic candidate.
 *
 * No heap, floating point, STM32 HAL type, persistent write, or recursion is
 * used. Median and MAD calculation reuse the caller workspace in place. The
 * public Step() API performs at most one hardware action or transition per
 * call, enabling cooperative scheduling.
 *
 * @par Time and fixed-point conventions
 * Internal physical time uses signed 64-bit picoseconds. DLY is stored in
 * device ticks of 250 ns. The launch/HIT period is modeled as
 * (DPL + 1) * 500 ns. WVR values remain raw unsigned Q1.7 ratios, where 128
 * represents 1.0. Rates are integer per mille in the inclusive range 0..1000.
 *
 * @par Selection policy
 * Gates decide whether a candidate is eligible; score only orders eligible
 * candidates and supplies "closest" failure evidence. A low score can never
 * override a failed physical or waveform gate.
 *
 * @warning This translation unit assumes serialized access to each calibrator
 *          and its bound driver. It is not ISR-safe or reentrant.
 */

#include "max35103_autocal.h"

#include <limits.h>
#include <string.h>

/** MAX35103 DLY register resolution: 250 ns expressed in picoseconds. */
#define AUTOCAL_PS_PER_DLY_TICK INT64_C(250000)
/** Increase in launch/HIT period per DPL count: 500 ns expressed in ps. */
#define AUTOCAL_PS_PER_DPL_UNIT INT64_C(500000)
/** Minimum consecutive HIT sequence used by generic DISCOVERY candidates. */
#define AUTOCAL_DISCOVERY_HITS 3U
/** Fast water sound-speed bound used for earliest expected arrival, in m/s. */
#define AUTOCAL_WATER_FAST_MPS UINT32_C(1600)
/** Slow water sound-speed bound used for latest expected arrival, in m/s. */
#define AUTOCAL_WATER_SLOW_MPS UINT32_C(1400)
/** Margin added around the sound-speed-derived arrival window, in ps. */
#define AUTOCAL_TOF_MARGIN_PS INT64_C(1000000)
/** Desired lead of the earliest coarse DLY point before direct arrival, in ps. */
#define AUTOCAL_DLY_LEAD_PS INT64_C(4000000)
/** Guard keeping the latest DLY point before earliest direct arrival, in ps. */
#define AUTOCAL_DLY_MAX_GUARD_PS INT64_C(1000000)

/* -------------------------------------------------------------------------- */
/* Normal-driver backend                                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Apply one candidate profile through the normal MAX35103 driver.
 *
 * @param[in] context Bound Max35103Driver pointer.
 * @param[in] profile Complete volatile candidate profile.
 * @return Status returned by MAX35103_Configure().
 */
static Max35103Status autocal_driver_configure(void *context, const Max35103Profile *profile)
{
    return MAX35103_Configure((Max35103Driver *)context, profile);
}

/**
 * @brief Collect one averaged TOF result and matching per-wave evidence.
 *
 * MAX35103_SelfCheck() executes the measurement and publishes its raw result in
 * the driver mailbox. The mailbox is consumed even when the measurement is
 * invalid so a later candidate cannot observe stale evidence. Wave evidence is
 * requested only after the averaged result is confirmed valid.
 *
 * @param[in] context Bound Max35103Driver pointer.
 * @param[out] result Averaged TOF result, cleared before use.
 * @param[out] wave Matching HIT/WVR evidence, cleared before use.
 * @return MAX35103_OK when both output objects contain usable evidence.
 * @return MAX35103_INVALID_ARG for any invalid pointer.
 * @return MAX35103_DEVICE_ERROR for a completed but driver-invalid result.
 * @return Another driver/mailbox status on transport or acquisition failure.
 */
static Max35103Status
autocal_driver_measure(void *context, Max35103RawResult *result, Max35103WaveEvidence *wave)
{
    Max35103Driver *driver = (Max35103Driver *)context;
    if (!driver || !result || !wave)
    {
        return MAX35103_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    memset(wave, 0, sizeof(*wave));

    const Max35103Status measurement_status = MAX35103_SelfCheck(driver);

    /*
     * SelfCheck publishes a mailbox even for a completed-but-invalid result.
     * Always consume it so the next candidate cannot be blocked by stale data.
     */
    Max35103Status mailbox_status = MAX35103_NO_RESULT;
    if (MAX35103_HasResult(driver))
    {
        mailbox_status = MAX35103_GetResult(driver, result);
    }
    if (measurement_status != MAX35103_OK)
    {
        return measurement_status;
    }
    if (mailbox_status != MAX35103_OK || !result->valid)
    {
        return mailbox_status == MAX35103_OK ? MAX35103_DEVICE_ERROR : mailbox_status;
    }

    return MAX35103_ReadWaveEvidence(driver, wave);
}

/**
 * @brief Reset the bound MAX35103 driver for independent verification.
 *
 * @param[in] context Bound Max35103Driver pointer.
 * @return Status returned by MAX35103_ResetDevice().
 */
static Max35103Status autocal_driver_reset(void *context)
{
    return MAX35103_ResetDevice((Max35103Driver *)context);
}

/* -------------------------------------------------------------------------- */
/* Range, register-field, and saturating-arithmetic helpers                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Count the inclusive values in an unsigned stepped range.
 *
 * @return floor((maximum - minimum) / step) + 1 for a valid range, otherwise
 *         zero. The final enumerated value may be below @p maximum when the
 *         interval is not exactly divisible by @p step.
 */
static uint32_t autocal_range_count_u32(uint32_t minimum, uint32_t maximum, uint32_t step)
{
    if (step == 0U || maximum < minimum)
    {
        return 0U;
    }
    return (maximum - minimum) / step + 1U;
}

/**
 * @brief Multiply candidate counts with saturation at UINT32_MAX.
 */
static uint32_t autocal_multiply_count(uint32_t left, uint32_t right)
{
    if (left == 0U || right == 0U)
    {
        return 0U;
    }
    if (left > UINT32_MAX / right)
    {
        return UINT32_MAX;
    }
    return left * right;
}

/**
 * @brief Add candidate counts with saturation at UINT32_MAX.
 */
static uint32_t autocal_add_count(uint32_t left, uint32_t right)
{
    if (left > UINT32_MAX - right)
    {
        return UINT32_MAX;
    }
    return left + right;
}

/**
 * @brief Count enabled bits in the low four bits of a mask.
 */
static uint8_t autocal_popcount4(uint8_t value)
{
    uint8_t count = 0U;
    for (uint8_t bit = 0U; bit < 4U; ++bit)
    {
        if ((value & (uint8_t)(1U << bit)) != 0U)
        {
            count++;
        }
    }
    return count;
}

/**
 * @brief Resolve a compact candidate index to an enabled CT value.
 *
 * Only CT bits 0..3 are meaningful. The caller first bounds @p index using
 * autocal_popcount4(), so the fallback zero return is defensive.
 */
static uint8_t autocal_ct_from_index(uint8_t mask, uint32_t index)
{
    for (uint8_t ct = 0U; ct < 4U; ++ct)
    {
        if ((mask & (uint8_t)(1U << ct)) == 0U)
        {
            continue;
        }
        if (index == 0U)
        {
            return ct;
        }
        index--;
    }
    return 0U;
}

/**
 * @brief Extract the DPL field from a candidate profile.
 */
static uint8_t autocal_profile_dpl(const Max35103Profile *profile)
{
    return (uint8_t)((profile->tof1 & MAX35103_TOF1_DPL_MASK) >> 4);
}

/**
 * @brief Extract the launch pulse count from a candidate profile.
 */
static uint8_t autocal_profile_pulse_count(const Max35103Profile *profile)
{
    return (uint8_t)(profile->tof1 >> 8);
}

/**
 * @brief Extract the stop-comparator polarity from a candidate profile.
 */
static bool autocal_profile_stop_polarity(const Max35103Profile *profile)
{
    return (profile->tof1 & MAX35103_TOF1_STOP_POL_MASK) != 0U;
}

/**
 * @brief Return the effective T2 receive-wave number.
 */
static uint8_t autocal_profile_t2(const Max35103Profile *profile)
{
    uint8_t wave = (uint8_t)((profile->tof2 & MAX35103_TOF2_T2WV_MASK) >> MAX35103_TOF2_T2WV_SHIFT);
    return wave < 2U ? 2U : wave;
}

/**
 * @brief Return the effective wave number for a configured receive hit.
 *
 * TOF3..TOF5 pack two HIT wave numbers per 16-bit word. The MAX35103 imposes
 * monotonically later minimum waves for later HIT positions; malformed lower
 * values are normalized to HIT index + 3 for timing reconstruction.
 */
static uint8_t autocal_profile_hit_wave(const Max35103Profile *profile, uint8_t hit_index)
{
    const uint16_t words[MAX35103_WAVE_HIT_COUNT / 2U] = {
        profile->tof3,
        profile->tof4,
        profile->tof5,
    };
    const uint16_t word = words[hit_index / 2U];
    uint8_t wave = (hit_index & 1U) == 0U ? (uint8_t)((word >> 8) & MAX35103_TOF_WAVE_SELECT_MASK)
                                          : (uint8_t)(word & MAX35103_TOF_WAVE_SELECT_MASK);
    const uint8_t earliest = (uint8_t)(hit_index + 3U);
    return wave < earliest ? earliest : wave;
}

/**
 * @brief Decode an unsigned comparator initial offset from a register word.
 */
static uint8_t autocal_profile_initial_offset(uint16_t word)
{
    return (uint8_t)(word & 0x007FU);
}

/**
 * @brief Decode a signed comparator return offset from a register word.
 */
static int8_t autocal_profile_return_offset(uint16_t word)
{
    return (int8_t)(uint8_t)(word >> 8);
}

/**
 * @brief Pack comparator return and initial offsets into one register word.
 *
 * The signed return offset occupies the high byte in two's-complement form.
 * The unsigned initial offset occupies bits 6:0; bit 7 is always cleared.
 */
static uint16_t autocal_offset_word(int8_t return_offset, uint8_t initial_offset)
{
    return (uint16_t)(((uint16_t)(uint8_t)return_offset << 8) | (uint16_t)(initial_offset & 0x7FU));
}

/**
 * @brief Update launch pulse, DPL, CT, and polarity fields in a profile.
 *
 * The complete TOF1 word is rebuilt intentionally. pulse_count occupies the
 * high byte, DPL bits 7:4, STOP_POL its defined mask bit, and CT bits 1:0.
 */
static void autocal_set_launch(
    Max35103Profile *profile, uint8_t dpl, uint8_t pulse_count, uint8_t ct, bool stop_polarity)
{
    profile->tof1 =
        (uint16_t)(((uint32_t)pulse_count << 8) | ((uint32_t)(dpl & 0x0FU) << 4) |
                   (stop_polarity ? (uint32_t)MAX35103_TOF1_STOP_POL_MASK : UINT32_C(0)) |
                   (uint32_t)(ct & 0x03U));
}

/**
 * @brief Build a consecutive receive-hit wave sequence in a profile.
 *
 * T2WV is clamped to 2..57 so six subsequent HIT wave numbers can remain
 * representable. HIT count is clamped to 1..MAX35103_WAVE_HIT_COUNT. Unrelated
 * low TOF2 control bits are preserved, while TOF3..TOF5 are rewritten as
 * T2WV+1 through T2WV+6. Fields beyond the configured HIT count remain
 * deterministic but are ignored by the device/driver.
 */
static void autocal_set_wave_sequence(Max35103Profile *profile, uint8_t t2_wave, uint8_t hit_count)
{
    if (t2_wave < 2U)
    {
        t2_wave = 2U;
    }
    if (t2_wave > 57U)
    {
        t2_wave = 57U;
    }
    if (hit_count < 1U)
    {
        hit_count = 1U;
    }
    if (hit_count > MAX35103_WAVE_HIT_COUNT)
    {
        hit_count = MAX35103_WAVE_HIT_COUNT;
    }

    const uint16_t preserved = profile->tof2 & (uint16_t)(0x0077U);
    profile->tof2 = (uint16_t)(preserved | ((uint16_t)(hit_count - 1U) << 13) |
                               ((uint16_t)t2_wave << MAX35103_TOF2_T2WV_SHIFT));

    const uint8_t hit1 = (uint8_t)(t2_wave + 1U);
    const uint8_t hit2 = (uint8_t)(t2_wave + 2U);
    const uint8_t hit3 = (uint8_t)(t2_wave + 3U);
    const uint8_t hit4 = (uint8_t)(t2_wave + 4U);
    const uint8_t hit5 = (uint8_t)(t2_wave + 5U);
    const uint8_t hit6 = (uint8_t)(t2_wave + 6U);
    profile->tof3 = (uint16_t)(((uint16_t)hit1 << 8) | hit2);
    profile->tof4 = (uint16_t)(((uint16_t)hit3 << 8) | hit4);
    profile->tof5 = (uint16_t)(((uint16_t)hit5 << 8) | hit6);
}

/**
 * @brief Clamp a signed value to an unsigned 16-bit range.
 */
static uint16_t autocal_clamp_u16(int32_t value, uint16_t minimum, uint16_t maximum)
{
    if (value < (int32_t)minimum)
    {
        return minimum;
    }
    if (value > (int32_t)maximum)
    {
        return maximum;
    }
    return (uint16_t)value;
}

/**
 * @brief Clamp a signed value to an unsigned 8-bit range.
 */
static uint8_t autocal_clamp_u8(int32_t value, uint8_t minimum, uint8_t maximum)
{
    if (value < (int32_t)minimum)
    {
        return minimum;
    }
    if (value > (int32_t)maximum)
    {
        return maximum;
    }
    return (uint8_t)value;
}

/**
 * @brief Return the absolute value of a signed 64-bit integer.
 *
 * Inputs originate from bounded timing differences and never equal INT64_MIN;
 * therefore unary negation cannot overflow under validated configuration.
 */
static int64_t autocal_abs_i64(int64_t value)
{
    return value < 0 ? -value : value;
}

/**
 * @brief Add two unsigned 64-bit values with saturation.
 */
static uint64_t autocal_sat_add_u64(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

/**
 * @brief Multiply two unsigned 64-bit values with saturation.
 */
static uint64_t autocal_sat_mul_u64(uint64_t left, uint64_t right)
{
    if (left == 0U || right == 0U)
    {
        return 0U;
    }
    return left > UINT64_MAX / right ? UINT64_MAX : left * right;
}

/* -------------------------------------------------------------------------- */
/* Contract validation and stage geometry                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Check that the auto-calibration backend provides all required operations.
 */
static bool autocal_backend_valid(const Max35103AutoCalBackend *backend)
{
    return backend != NULL && backend->configure != NULL && backend->measure != NULL &&
           backend->reset != NULL;
}

/**
 * @brief Validate search ranges, gates, limits, and workspace requirements.
 *
 * Validation is deliberately centralized so Init() and Start() enforce the
 * same contract. Besides register-field limits, this verifies nonzero steps,
 * ordered inclusive ranges, 0..1000 per-mille gates, Q1.7 bound ordering,
 * nonnegative dispersion limits, retry/fallback consistency, and sufficient
 * caller workspace for every configured batch size.
 */
static bool autocal_config_valid(const Max35103AutoCalConfig *config, uint16_t sample_capacity)
{
    if (!config || config->acoustic_path_length_um == 0U || config->expected_min_tof_ps <= 0 ||
        config->expected_max_tof_ps <= config->expected_min_tof_ps || config->dpl_min == 0U ||
        config->dpl_max > 15U || config->dpl_max < config->dpl_min ||
        config->pulse_count_min == 0U || config->pulse_count_max > 127U ||
        config->pulse_count_max < config->pulse_count_min || config->pulse_count_step == 0U ||
        (config->ct_mask & 0x0FU) == 0U || config->dly_min < MAX35103_TOF_DELAY_MIN ||
        config->dly_max < config->dly_min || config->dly_coarse_step == 0U ||
        config->dly_fine_step == 0U || config->initial_offset_max > 127U ||
        config->initial_offset_max < config->initial_offset_min ||
        config->initial_offset_coarse_step == 0U || config->initial_offset_fine_step == 0U ||
        config->return_offset_max < config->return_offset_min || config->return_offset_step == 0U ||
        config->t2_wave_min < 2U || config->t2_wave_max > 57U ||
        config->t2_wave_max < config->t2_wave_min || config->hit_count_min < 2U ||
        config->hit_count_max > MAX35103_WAVE_HIT_COUNT ||
        config->hit_count_max < config->hit_count_min || config->samples_per_candidate == 0U ||
        config->finalist_samples == 0U || config->verification_samples == 0U ||
        config->samples_per_candidate > sample_capacity ||
        config->finalist_samples > sample_capacity ||
        config->verification_samples > sample_capacity ||
        config->min_valid_rate_per_mille > 1000U ||
        config->min_tuning_physical_rate_per_mille > 1000U ||
        config->min_physical_rate_per_mille > 1000U ||
        config->min_wave_valid_rate_per_mille > 1000U ||
        config->min_wvr_good_rate_per_mille > 1000U || config->wvr_t1_t2_min_q7 == 0U ||
        config->wvr_t2_ideal_min_q7 == 0U || config->wvr_ratio_max_q7 < config->wvr_t1_t2_min_q7 ||
        config->wvr_ratio_max_q7 < config->wvr_t2_ideal_min_q7 || config->max_tof_mad_ps < 0 ||
        config->max_diff_mad_ps < 0 || config->max_period_error_ps < 0 ||
        config->max_direction_delta_ps <= 0 || config->max_cycle_slip_rate_per_mille > 1000U ||
        config->required_perturbation_passes > MAX35103_AUTOCAL_PERTURBATION_COUNT ||
        config->max_profile_fallbacks >= MAX35103_AUTOCAL_DISCOVERY_FINALISTS ||
        config->max_consecutive_driver_errors == 0U || config->max_busy_polls == 0U)
    {
        return false;
    }
    return true;
}

/**
 * @brief Build a bounded 16-bit fine-search range around a center value.
 */
static void autocal_fine_range_u16(const Max35103AutoCalConfig *config,
                                   uint16_t center,
                                   uint16_t radius,
                                   uint16_t *minimum,
                                   uint16_t *maximum)
{
    const int32_t low = (int32_t)center - (int32_t)radius;
    const int32_t high = (int32_t)center + (int32_t)radius;
    *minimum = autocal_clamp_u16(low, config->dly_min, config->dly_max);
    *maximum = autocal_clamp_u16(high, config->dly_min, config->dly_max);
}

/**
 * @brief Build a bounded 8-bit fine-search range around a center value.
 */
static void autocal_fine_range_u8(const Max35103AutoCalConfig *config,
                                  uint8_t center,
                                  uint8_t radius,
                                  uint8_t *minimum,
                                  uint8_t *maximum)
{
    const int32_t low = (int32_t)center - (int32_t)radius;
    const int32_t high = (int32_t)center + (int32_t)radius;
    *minimum = autocal_clamp_u8(low, config->initial_offset_min, config->initial_offset_max);
    *maximum = autocal_clamp_u8(high, config->initial_offset_min, config->initial_offset_max);
}

/**
 * @brief Calculate the candidate count for one auto-calibration stage.
 *
 * DISCOVERY is the Cartesian product
 *
 * @code
 * DPL x pulse count x enabled CT x polarity x coarse DLY
 * @endcode
 *
 * plus candidate zero containing the untouched seed profile. Subsequent
 * stages search one parameter dimension, except WAVE_SELECT which crosses
 * T2WV with HIT count and ROBUSTNESS which has a fixed perturbation set.
 * Saturating arithmetic keeps diagnostic counts defined even for an
 * impractically large user configuration.
 */
static uint32_t autocal_candidate_count(const Max35103AutoCalibrator *calibrator,
                                        Max35103AutoCalState state)
{
    const Max35103AutoCalConfig *config = &calibrator->config;
    switch (state)
    {
    case MAX35103_AUTOCAL_STATE_DISCOVERY: {
        uint32_t count = autocal_range_count_u32(config->dpl_min, config->dpl_max, 1U);
        count = autocal_multiply_count(count,
                                       autocal_range_count_u32(config->pulse_count_min,
                                                               config->pulse_count_max,
                                                               config->pulse_count_step));
        count = autocal_multiply_count(count, autocal_popcount4(config->ct_mask));
        count = autocal_multiply_count(count, config->try_both_polarities ? 2U : 1U);
        count = autocal_multiply_count(
            count,
            autocal_range_count_u32(config->dly_min, config->dly_max, config->dly_coarse_step));
        /*
         * Candidate zero is the complete caller-provided seed.  In
         * particular, preserve TOF2..TOF7 because the selected HIT waves
         * affect the reconstructed wave-zero arrival.  The remaining
         * candidates are the generic launch/DLY search grid.
         */
        return autocal_add_count(count, 1U);
    }
    case MAX35103_AUTOCAL_STATE_BIAS_CHARGE:
        return autocal_popcount4(config->ct_mask);
    case MAX35103_AUTOCAL_STATE_DLY_FINE: {
        uint16_t minimum;
        uint16_t maximum;
        autocal_fine_range_u16(config,
                               calibrator->stage_base_profile.tof_measurement_delay,
                               config->dly_coarse_step,
                               &minimum,
                               &maximum);
        return autocal_range_count_u32(minimum, maximum, config->dly_fine_step);
    }
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_COARSE:
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_COARSE:
        return autocal_range_count_u32(config->initial_offset_min,
                                       config->initial_offset_max,
                                       config->initial_offset_coarse_step);
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_FINE:
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_FINE: {
        const uint16_t word = state == MAX35103_AUTOCAL_STATE_OFFSET_UP_FINE
                                  ? calibrator->stage_base_profile.tof6
                                  : calibrator->stage_base_profile.tof7;
        uint8_t minimum;
        uint8_t maximum;
        autocal_fine_range_u8(config,
                              autocal_profile_initial_offset(word),
                              config->initial_offset_coarse_step,
                              &minimum,
                              &maximum);
        return autocal_range_count_u32(minimum, maximum, config->initial_offset_fine_step);
    }
    case MAX35103_AUTOCAL_STATE_WAVE_SELECT:
        return autocal_multiply_count(
            autocal_range_count_u32(config->t2_wave_min, config->t2_wave_max, 1U),
            autocal_range_count_u32(config->hit_count_min, config->hit_count_max, 1U));
    case MAX35103_AUTOCAL_STATE_RETURN_UP:
    case MAX35103_AUTOCAL_STATE_RETURN_DOWN:
        return autocal_range_count_u32((uint32_t)((int32_t)config->return_offset_min + 128),
                                       (uint32_t)((int32_t)config->return_offset_max + 128),
                                       config->return_offset_step);
    case MAX35103_AUTOCAL_STATE_VERIFY:
    case MAX35103_AUTOCAL_STATE_RESET_VERIFY:
        return 1U;
    case MAX35103_AUTOCAL_STATE_ROBUSTNESS:
        return MAX35103_AUTOCAL_PERTURBATION_COUNT;
    default:
        return 0U;
    }
}

/**
 * @brief Return the measurement batch size required by the current stage.
 *
 * WAVE_SELECT and signed return-offset stages receive the longer finalist
 * batch because they directly determine production wave tracking. VERIFY and
 * RESET_VERIFY use the largest independent batch. Other search stages use the
 * short per-candidate batch.
 */
static uint16_t autocal_sample_target(const Max35103AutoCalibrator *calibrator)
{
    switch (calibrator->state)
    {
    case MAX35103_AUTOCAL_STATE_VERIFY:
    case MAX35103_AUTOCAL_STATE_RESET_VERIFY:
        return calibrator->config.verification_samples;

    case MAX35103_AUTOCAL_STATE_WAVE_SELECT:
    case MAX35103_AUTOCAL_STATE_RETURN_UP:
    case MAX35103_AUTOCAL_STATE_RETURN_DOWN:
        return calibrator->config.finalist_samples;

    default:
        return calibrator->config.samples_per_candidate;
    }
}

/**
 * @brief Enter a search stage and reset its stage-local bookkeeping.
 *
 * @c selected_profile is snapshotted into @c stage_base_profile. Every
 * candidate in the new stage is derived from that immutable base, preventing
 * earlier candidates from contaminating later candidate generation.
 * Cross-stage totals, discovery finalists, fallback counts, and report state
 * are intentionally preserved.
 */
static void autocal_enter_state(Max35103AutoCalibrator *calibrator, Max35103AutoCalState state)
{
    calibrator->state = state;
    calibrator->stage_base_profile = calibrator->selected_profile;
    calibrator->candidate_index = 0U;
    calibrator->candidate_count = autocal_candidate_count(calibrator, state);
    calibrator->sample_index = 0U;
    calibrator->sample_target = autocal_sample_target(calibrator);
    calibrator->stage_retry_count = 0U;
    calibrator->stage_best_valid = false;
    calibrator->stage_closest_valid = false;
    calibrator->candidate_started = false;
    calibrator->candidate_configured = false;
    calibrator->busy_poll_count = 0U;
    calibrator->reset_performed = false;
    memset(&calibrator->candidate_metrics, 0, sizeof(calibrator->candidate_metrics));
    memset(&calibrator->stage_best_metrics, 0, sizeof(calibrator->stage_best_metrics));
    memset(&calibrator->stage_closest_metrics, 0, sizeof(calibrator->stage_closest_metrics));
}

/**
 * @brief Restart the current search stage after a recoverable failure.
 *
 * The committed selected profile is retained. Candidate enumeration and
 * stage-local winners are cleared. DISCOVERY additionally clears its finalist
 * set because all finalists must come from the same complete retry pass.
 */
static void autocal_restart_stage(Max35103AutoCalibrator *calibrator)
{
    if (calibrator->state == MAX35103_AUTOCAL_STATE_DISCOVERY)
    {
        calibrator->discovery_finalist_count = 0U;
        calibrator->discovery_finalist_index = 0U;
        memset(calibrator->discovery_finalist_profiles,
               0,
               sizeof(calibrator->discovery_finalist_profiles));
        memset(calibrator->discovery_finalist_metrics,
               0,
               sizeof(calibrator->discovery_finalist_metrics));
    }
    calibrator->candidate_index = 0U;
    calibrator->sample_index = 0U;
    calibrator->sample_target = autocal_sample_target(calibrator);
    if (calibrator->stage_retry_count < UINT8_MAX)
    {
        calibrator->stage_retry_count++;
    }
    calibrator->stage_best_valid = false;
    calibrator->stage_closest_valid = false;
    calibrator->candidate_started = false;
    calibrator->candidate_configured = false;
    calibrator->busy_poll_count = 0U;
    calibrator->reset_performed = false;
    memset(&calibrator->candidate_metrics, 0, sizeof(calibrator->candidate_metrics));
    memset(&calibrator->stage_best_metrics, 0, sizeof(calibrator->stage_best_metrics));
    memset(&calibrator->stage_closest_metrics, 0, sizeof(calibrator->stage_closest_metrics));
}

/**
 * @brief Retry the current stage when its retry budget permits.
 */
static bool autocal_retry_stage_if_allowed(Max35103AutoCalibrator *calibrator)
{
    if (calibrator->stage_retry_count >= calibrator->config.max_stage_retries)
    {
        return false;
    }
    autocal_restart_stage(calibrator);
    return true;
}

/**
 * @brief Generate the profile represented by a stage-local candidate index.
 *
 * @param[in,out] calibrator Active stage and immutable stage-base profile.
 * @param[in] index Zero-based index in the current stage's candidate space.
 * @param[out] candidate Complete profile derived from the stage base.
 *
 * @return true when the generated profile passes MAX35103_ValidateProfile().
 * @return false for an unsupported state, an unrealizable edge perturbation,
 *         or a profile rejected by the normal driver validator.
 *
 * Every branch modifies only the register field owned by its stage. This is
 * important: a winner from the preceding stage remains fixed while the next
 * coordinate is searched.
 */
static bool autocal_make_candidate(Max35103AutoCalibrator *calibrator,
                                   uint32_t index,
                                   Max35103Profile *candidate)
{
    const Max35103AutoCalConfig *config = &calibrator->config;
    *candidate = calibrator->stage_base_profile;

    switch (calibrator->state)
    {
    case MAX35103_AUTOCAL_STATE_DISCOVERY: {
        /*
         * Candidate zero is the exact seed, including its existing TOF2..TOF7
         * wave and comparator fields. Remaining indices decode a mixed-radix
         * Cartesian grid. DLY is the fastest-changing dimension, followed by
         * polarity, pulse count, CT, then DPL.
         */
        if (index == 0U)
        {
            *candidate = calibrator->seed_profile;
            break;
        }
        index--;

        const uint32_t dly_count =
            autocal_range_count_u32(config->dly_min, config->dly_max, config->dly_coarse_step);
        const uint32_t polarity_count = config->try_both_polarities ? 2U : 1U;
        const uint32_t pulse_count = autocal_range_count_u32(
            config->pulse_count_min, config->pulse_count_max, config->pulse_count_step);
        const uint32_t ct_count = autocal_popcount4(config->ct_mask);

        const uint32_t dly_index = index % dly_count;
        index /= dly_count;
        const uint32_t polarity_index = index % polarity_count;
        index /= polarity_count;
        const uint32_t pulse_index = index % pulse_count;
        index /= pulse_count;
        const uint32_t ct_index = index % ct_count;
        index /= ct_count;
        const uint32_t dpl_index = index;

        const uint8_t dpl = (uint8_t)(config->dpl_min + dpl_index);
        const uint8_t pulses =
            (uint8_t)(config->pulse_count_min + pulse_index * config->pulse_count_step);
        const uint8_t charge_time = autocal_ct_from_index(config->ct_mask, ct_index);
        const bool polarity = config->try_both_polarities
                                  ? polarity_index != 0U
                                  : autocal_profile_stop_polarity(&calibrator->seed_profile);
        autocal_set_launch(candidate, dpl, pulses, charge_time, polarity);
        candidate->tof_measurement_delay =
            (uint16_t)(config->dly_min + dly_index * config->dly_coarse_step);

        /*
         * Generic discovery candidates use one deterministic early receive
         * sequence. Wave choice is tuned later after a viable launch family
         * and receive delay have been established.
         */
        uint8_t discovery_hits = config->hit_count_min;
        if (discovery_hits < AUTOCAL_DISCOVERY_HITS)
        {
            discovery_hits = AUTOCAL_DISCOVERY_HITS;
        }
        if (discovery_hits > config->hit_count_max)
        {
            discovery_hits = config->hit_count_max;
        }
        autocal_set_wave_sequence(candidate, config->t2_wave_min, discovery_hits);
        break;
    }
    case MAX35103_AUTOCAL_STATE_BIAS_CHARGE:
        /* Sweep only CT; retain discovery DPL, pulse count, polarity, and DLY. */
        autocal_set_launch(candidate,
                           autocal_profile_dpl(candidate),
                           autocal_profile_pulse_count(candidate),
                           autocal_ct_from_index(config->ct_mask, index),
                           autocal_profile_stop_polarity(candidate));
        break;
    case MAX35103_AUTOCAL_STATE_DLY_FINE: {
        /* Search one coarse-step radius around the committed coarse DLY. */
        uint16_t minimum;
        uint16_t maximum;
        autocal_fine_range_u16(config,
                               calibrator->stage_base_profile.tof_measurement_delay,
                               config->dly_coarse_step,
                               &minimum,
                               &maximum);
        (void)maximum;
        candidate->tof_measurement_delay = (uint16_t)(minimum + index * config->dly_fine_step);
        break;
    }
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_COARSE: {
        /* TOF6 low seven bits hold the UP initial comparator offset. */
        const uint8_t initial =
            (uint8_t)(config->initial_offset_min + index * config->initial_offset_coarse_step);
        candidate->tof6 =
            autocal_offset_word(autocal_profile_return_offset(candidate->tof6), initial);
        break;
    }
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_FINE: {
        /* Refine within one coarse-step radius around the coarse UP winner. */
        uint8_t minimum;
        uint8_t maximum;
        autocal_fine_range_u8(config,
                              autocal_profile_initial_offset(calibrator->stage_base_profile.tof6),
                              config->initial_offset_coarse_step,
                              &minimum,
                              &maximum);
        (void)maximum;
        candidate->tof6 =
            autocal_offset_word(autocal_profile_return_offset(candidate->tof6),
                                (uint8_t)(minimum + index * config->initial_offset_fine_step));
        break;
    }
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_COARSE: {
        /* TOF7 low seven bits hold the DOWN initial comparator offset. */
        const uint8_t initial =
            (uint8_t)(config->initial_offset_min + index * config->initial_offset_coarse_step);
        candidate->tof7 =
            autocal_offset_word(autocal_profile_return_offset(candidate->tof7), initial);
        break;
    }
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_FINE: {
        /* Refine within one coarse-step radius around the coarse DOWN winner. */
        uint8_t minimum;
        uint8_t maximum;
        autocal_fine_range_u8(config,
                              autocal_profile_initial_offset(calibrator->stage_base_profile.tof7),
                              config->initial_offset_coarse_step,
                              &minimum,
                              &maximum);
        (void)maximum;
        candidate->tof7 =
            autocal_offset_word(autocal_profile_return_offset(candidate->tof7),
                                (uint8_t)(minimum + index * config->initial_offset_fine_step));
        break;
    }
    case MAX35103_AUTOCAL_STATE_WAVE_SELECT: {
        /*
         * HIT count changes fastest inside each T2WV value. The helper creates
         * consecutive HIT1..HIT6 numbers following T2WV.
         */
        const uint32_t hit_count_values =
            autocal_range_count_u32(config->hit_count_min, config->hit_count_max, 1U);
        const uint8_t hits = (uint8_t)(config->hit_count_min + index % hit_count_values);
        const uint8_t t2 = (uint8_t)(config->t2_wave_min + index / hit_count_values);
        autocal_set_wave_sequence(candidate, t2, hits);
        break;
    }
    case MAX35103_AUTOCAL_STATE_RETURN_UP: {
        /* Sweep signed TOF6 high byte while retaining the selected UP initial offset. */
        const int8_t value = (int8_t)((int32_t)config->return_offset_min +
                                      (int32_t)(index * config->return_offset_step));
        candidate->tof6 =
            autocal_offset_word(value, autocal_profile_initial_offset(candidate->tof6));
        break;
    }
    case MAX35103_AUTOCAL_STATE_RETURN_DOWN: {
        /* Sweep signed TOF7 high byte while retaining the selected DOWN initial offset. */
        const int8_t value = (int8_t)((int32_t)config->return_offset_min +
                                      (int32_t)(index * config->return_offset_step));
        candidate->tof7 =
            autocal_offset_word(value, autocal_profile_initial_offset(candidate->tof7));
        break;
    }
    case MAX35103_AUTOCAL_STATE_ROBUSTNESS: {
        /*
         * Fixed eight-point neighborhood:
         *   0..1  DLY -/+ one fine step
         *   2..3  UP initial offset -/+ one fine step
         *   4..5  DOWN initial offset -/+ one fine step
         *   6..7  T2WV -/+ one wave
         *
         * At a range boundary the code tries the opposite two-step direction.
         * A perturbation still equal to the base is unrealizable and skipped;
         * it is not counted as tested or failed.
         */
        const int32_t dly_step = config->dly_fine_step;
        const int32_t offset_step = config->initial_offset_fine_step;
        if (index == 0U || index == 1U)
        {
            const uint16_t original = candidate->tof_measurement_delay;
            candidate->tof_measurement_delay = autocal_clamp_u16(
                (int32_t)candidate->tof_measurement_delay + (index == 0U ? -dly_step : dly_step),
                config->dly_min,
                config->dly_max);
            if (candidate->tof_measurement_delay == original)
            {
                candidate->tof_measurement_delay = autocal_clamp_u16(
                    (int32_t)original + (index == 0U ? 2 * dly_step : -2 * dly_step),
                    config->dly_min,
                    config->dly_max);
            }
            if (candidate->tof_measurement_delay == original)
            {
                return false;
            }
        }
        else if (index == 2U || index == 3U)
        {
            const uint8_t original = autocal_profile_initial_offset(candidate->tof6);
            uint8_t perturbed =
                autocal_clamp_u8((int32_t)original + (index == 2U ? -offset_step : offset_step),
                                 config->initial_offset_min,
                                 config->initial_offset_max);
            if (perturbed == original)
            {
                perturbed = autocal_clamp_u8((int32_t)original +
                                                 (index == 2U ? 2 * offset_step : -2 * offset_step),
                                             config->initial_offset_min,
                                             config->initial_offset_max);
            }
            if (perturbed == original)
            {
                return false;
            }
            candidate->tof6 =
                autocal_offset_word(autocal_profile_return_offset(candidate->tof6), perturbed);
        }
        else if (index == 4U || index == 5U)
        {
            const uint8_t original = autocal_profile_initial_offset(candidate->tof7);
            uint8_t perturbed =
                autocal_clamp_u8((int32_t)original + (index == 4U ? -offset_step : offset_step),
                                 config->initial_offset_min,
                                 config->initial_offset_max);
            if (perturbed == original)
            {
                perturbed = autocal_clamp_u8((int32_t)original +
                                                 (index == 4U ? 2 * offset_step : -2 * offset_step),
                                             config->initial_offset_min,
                                             config->initial_offset_max);
            }
            if (perturbed == original)
            {
                return false;
            }
            candidate->tof7 =
                autocal_offset_word(autocal_profile_return_offset(candidate->tof7), perturbed);
        }
        else
        {
            const int32_t wave_delta = index == 6U ? -1 : 1;
            const uint8_t original = autocal_profile_t2(candidate);
            uint8_t t2 = autocal_clamp_u8(
                (int32_t)original + wave_delta, config->t2_wave_min, config->t2_wave_max);
            if (t2 == original)
            {
                t2 = autocal_clamp_u8(
                    (int32_t)original - 2 * wave_delta, config->t2_wave_min, config->t2_wave_max);
            }
            if (t2 == original)
            {
                return false;
            }
            autocal_set_wave_sequence(candidate, t2, MAX35103_ConfiguredHitCount(candidate));
        }
        break;
    }
    case MAX35103_AUTOCAL_STATE_VERIFY:
    case MAX35103_AUTOCAL_STATE_RESET_VERIFY:
        /* The single candidate is the unmodified committed selected profile. */
        break;
    default:
        return false;
    }

    return MAX35103_ValidateProfile(candidate) == MAX35103_OK;
}

/* -------------------------------------------------------------------------- */
/* In-place robust statistics                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Swap two workspace samples.
 */
static void autocal_swap_samples(Max35103AutoCalSample *left, Max35103AutoCalSample *right)
{
    const Max35103AutoCalSample temporary = *left;
    *left = *right;
    *right = temporary;
}

/**
 * @brief Select the kth workspace value using in-place partitioning.
 *
 * This quickselect variant reorders entries but requires O(1) extra storage.
 * The caller never relies on original sample order after a batch completes.
 *
 * @pre @p samples points to at least @p count entries.
 * @pre @p count is nonzero and @p kth is less than @p count.
 * @return The kth order statistic from the current @c work_ps values.
 */
static int64_t autocal_select_work(Max35103AutoCalSample *samples, uint16_t count, uint16_t kth)
{
    uint16_t left = 0U;
    uint16_t right = (uint16_t)(count - 1U);

    while (left < right)
    {
        const int64_t pivot = samples[left + (uint16_t)((right - left) / 2U)].work_ps;
        uint16_t i = left;
        uint16_t j = right;

        while (i <= j)
        {
            while (samples[i].work_ps < pivot)
            {
                i++;
            }
            while (samples[j].work_ps > pivot)
            {
                if (j == 0U)
                {
                    break;
                }
                j--;
            }
            if (i <= j)
            {
                autocal_swap_samples(&samples[i], &samples[j]);
                i++;
                if (j == 0U)
                {
                    break;
                }
                j--;
            }
        }

        if (kth <= j)
        {
            right = j;
        }
        else if (kth >= i)
        {
            left = i;
        }
        else
        {
            break;
        }
    }
    return samples[kth].work_ps;
}

/**
 * @brief Calculate the median of the workspace work values.
 *
 * For an even count the result is the midpoint between the two central order
 * statistics. The subtraction-first expression reduces overflow risk.
 */
static int64_t autocal_work_median(Max35103AutoCalSample *samples, uint16_t count)
{
    const uint16_t upper_index = (uint16_t)(count / 2U);
    const int64_t upper = autocal_select_work(samples, count, upper_index);
    if ((count & 1U) != 0U)
    {
        return upper;
    }
    const int64_t lower = autocal_select_work(samples, count, (uint16_t)(upper_index - 1U));
    return lower + (upper - lower) / 2;
}

/**
 * @brief Workspace sample fields supported by median and MAD calculations.
 */
typedef enum
{
    /** Reconstructed upstream wave-zero arrival. */
    AUTOCAL_FIELD_UP = 0,
    /** Reconstructed downstream wave-zero arrival. */
    AUTOCAL_FIELD_DOWN,
    /** Device-provided averaged TOF difference. */
    AUTOCAL_FIELD_DIFF,
    /** Adjacent-HIT period error. */
    AUTOCAL_FIELD_PERIOD,
} AutoCalMetricField;

/**
 * @brief Read one metric field from a workspace sample.
 */
static int64_t autocal_sample_field(const Max35103AutoCalSample *sample, AutoCalMetricField field)
{
    switch (field)
    {
    case AUTOCAL_FIELD_UP:
        return sample->tof_up_ps;
    case AUTOCAL_FIELD_DOWN:
        return sample->tof_down_ps;
    case AUTOCAL_FIELD_DIFF:
        return sample->tof_diff_ps;
    case AUTOCAL_FIELD_PERIOD:
    default:
        return sample->period_error_ps;
    }
}

/**
 * @brief Calculate the median of one sample field.
 *
 * The selected source field is copied into each entry's @c work_ps scratch
 * member before in-place quickselect.
 */
static int64_t
autocal_median_field(Max35103AutoCalSample *samples, uint16_t count, AutoCalMetricField field)
{
    for (uint16_t i = 0U; i < count; ++i)
    {
        samples[i].work_ps = autocal_sample_field(&samples[i], field);
    }
    return autocal_work_median(samples, count);
}

/**
 * @brief Calculate median absolute deviation for one sample field.
 *
 * MAD = median(|x_i - median(x)|). It is used instead of standard deviation so
 * isolated bad acoustic captures do not dominate candidate stability.
 */
static int64_t autocal_mad_field(Max35103AutoCalSample *samples,
                                 uint16_t count,
                                 AutoCalMetricField field,
                                 int64_t median)
{
    for (uint16_t i = 0U; i < count; ++i)
    {
        samples[i].work_ps = autocal_abs_i64(autocal_sample_field(&samples[i], field) - median);
    }
    return autocal_work_median(samples, count);
}

/**
 * @brief Convert a count ratio to a saturated per-mille rate.
 *
 * Integer half-denominator bias implements rounding to nearest per mille.
 * Numerators are counts from the same batch and therefore do not exceed the
 * denominator under normal operation.
 */
static uint16_t autocal_per_mille(uint16_t numerator, uint16_t denominator)
{
    if (denominator == 0U)
    {
        return 0U;
    }
    return (uint16_t)(((uint32_t)numerator * 1000U + (uint32_t)denominator / 2U) / denominator);
}

/* -------------------------------------------------------------------------- */
/* Metric gates, scoring, and candidate retention                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Evaluate launch-period consistency for candidate metrics.
 *
 * The period gate requires sufficient samples with usable per-HIT evidence
 * before comparing the median period error to its picosecond limit.
 */
static bool autocal_period_gate(const Max35103AutoCalibrator *calibrator,
                                const Max35103AutoCalMetrics *metrics)
{
    return metrics->valid_count != 0U &&
           metrics->wave_valid_rate_per_mille >= calibrator->config.min_wave_valid_rate_per_mille &&
           metrics->median_period_error_ps <= calibrator->config.max_period_error_ps;
}

/**
 * @brief Return the physical-rate threshold for the active stage.
 */
static uint16_t autocal_required_physical_rate(const Max35103AutoCalibrator *calibrator)
{
    switch (calibrator->state)
    {
    case MAX35103_AUTOCAL_STATE_BIAS_CHARGE:
    case MAX35103_AUTOCAL_STATE_DLY_FINE:
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_COARSE:
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_FINE:
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_COARSE:
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_FINE:
    case MAX35103_AUTOCAL_STATE_WAVE_SELECT:
    case MAX35103_AUTOCAL_STATE_RETURN_UP:
    case MAX35103_AUTOCAL_STATE_RETURN_DOWN:
        return calibrator->config.min_tuning_physical_rate_per_mille;

    default:
        return calibrator->config.min_physical_rate_per_mille;
    }
}

/**
 * @brief Select the WVR success rate relevant to the current tuning stage.
 *
 * Early launch/DLY stages intentionally do not reject on WVR because comparator
 * offsets and wave selection have not yet been tuned. UP offset stages inspect
 * upstream WVR; DOWN stages require the worse direction; later/final stages use
 * the bidirectional WVR rate.
 */
static uint16_t autocal_relevant_wvr_rate(const Max35103AutoCalibrator *calibrator,
                                          const Max35103AutoCalMetrics *metrics)
{
    switch (calibrator->state)
    {
    case MAX35103_AUTOCAL_STATE_DISCOVERY:
    case MAX35103_AUTOCAL_STATE_BIAS_CHARGE:
    case MAX35103_AUTOCAL_STATE_DLY_FINE:
        return 1000U;

    case MAX35103_AUTOCAL_STATE_OFFSET_UP_COARSE:
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_FINE:
        return metrics->wvr_up_good_rate_per_mille;

    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_COARSE:
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_FINE:
        return metrics->wvr_up_good_rate_per_mille < metrics->wvr_down_good_rate_per_mille
                   ? metrics->wvr_up_good_rate_per_mille
                   : metrics->wvr_down_good_rate_per_mille;

    default:
        return metrics->wvr_good_rate_per_mille;
    }
}

/**
 * @brief Evaluate stage-specific waveform and WVR acceptance criteria.
 *
 * All stages require period coherence. Early stages stop there, directional
 * offset stages apply only the WVR evidence already meaningful at that point,
 * and final stages require the full bidirectional waveform gate.
 */
static bool autocal_stage_waveform_gate(const Max35103AutoCalibrator *calibrator,
                                        const Max35103AutoCalMetrics *metrics)
{
    if (!autocal_period_gate(calibrator, metrics))
    {
        return false;
    }

    switch (calibrator->state)
    {
    case MAX35103_AUTOCAL_STATE_DISCOVERY:
    case MAX35103_AUTOCAL_STATE_BIAS_CHARGE:
    case MAX35103_AUTOCAL_STATE_DLY_FINE:
        return true;

    case MAX35103_AUTOCAL_STATE_OFFSET_UP_COARSE:
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_FINE:
        return metrics->wvr_up_good_rate_per_mille >=
               calibrator->config.min_wvr_good_rate_per_mille;

    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_COARSE:
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_FINE:
        return metrics->wvr_up_good_rate_per_mille >=
                   calibrator->config.min_wvr_good_rate_per_mille &&
               metrics->wvr_down_good_rate_per_mille >=
                   calibrator->config.min_wvr_good_rate_per_mille;

    default:
        return metrics->waveform_gate;
    }
}

/**
 * @brief Aggregate sample statistics, validation gates, and candidate score.
 *
 * Processing order is:
 *
 * 1. count independent evidence flags across all attempts;
 * 2. compact valid entries to the beginning of the borrowed workspace;
 * 3. calculate per-mille rates;
 * 4. calculate TOF medians/MADs from valid entries;
 * 5. detect cycle slips relative to direction medians;
 * 6. compact wave-valid entries and calculate period statistics;
 * 7. evaluate gates;
 * 8. build a saturated lower-is-better score.
 *
 * Compaction and quickselect reorder the batch in place. No later operation
 * depends on chronological sample order.
 */
static void autocal_finalize_metrics(Max35103AutoCalibrator *calibrator,
                                     Max35103AutoCalMetrics *metrics)
{
    metrics->attempted_count = calibrator->sample_index;
    metrics->valid_count = 0U;
    metrics->physical_count = 0U;
    metrics->wave_valid_count = 0U;
    metrics->wvr_up_good_count = 0U;
    metrics->wvr_down_good_count = 0U;
    metrics->wvr_good_count = 0U;
    metrics->cycle_slip_count = 0U;

    for (uint16_t i = 0U; i < calibrator->sample_index; ++i)
    {
        const uint8_t flags = calibrator->samples[i].flags;
        if ((flags & MAX35103_AUTOCAL_SAMPLE_VALID) != 0U)
        {
            metrics->valid_count++;
        }
        if ((flags & MAX35103_AUTOCAL_SAMPLE_PHYSICAL) != 0U)
        {
            metrics->physical_count++;
        }
        if ((flags & MAX35103_AUTOCAL_SAMPLE_WAVE_VALID) != 0U)
        {
            metrics->wave_valid_count++;
        }
        if ((flags & MAX35103_AUTOCAL_SAMPLE_WVR_UP_GOOD) != 0U)
        {
            metrics->wvr_up_good_count++;
        }
        if ((flags & MAX35103_AUTOCAL_SAMPLE_WVR_DN_GOOD) != 0U)
        {
            metrics->wvr_down_good_count++;
        }
        if ((flags & MAX35103_AUTOCAL_SAMPLE_WVR_GOOD) != 0U)
        {
            metrics->wvr_good_count++;
        }
    }

    /*
     * Compact valid entries before running in-place quickselect. Invalid
     * transport/device attempts remain represented in attempted_count and
     * rates, but cannot inject zero-filled timing values into robust statistics.
     */
    uint16_t valid_end = 0U;
    for (uint16_t i = 0U; i < calibrator->sample_index; ++i)
    {
        if ((calibrator->samples[i].flags & MAX35103_AUTOCAL_SAMPLE_VALID) != 0U)
        {
            if (i != valid_end)
            {
                autocal_swap_samples(&calibrator->samples[i], &calibrator->samples[valid_end]);
            }
            valid_end++;
        }
    }

    metrics->valid_rate_per_mille =
        autocal_per_mille(metrics->valid_count, metrics->attempted_count);
    metrics->physical_rate_per_mille =
        autocal_per_mille(metrics->physical_count, metrics->attempted_count);
    metrics->physical_rate_required_per_mille = autocal_required_physical_rate(calibrator);
    metrics->wave_valid_rate_per_mille =
        autocal_per_mille(metrics->wave_valid_count, metrics->attempted_count);
    metrics->wvr_up_good_rate_per_mille =
        autocal_per_mille(metrics->wvr_up_good_count, metrics->valid_count);
    metrics->wvr_down_good_rate_per_mille =
        autocal_per_mille(metrics->wvr_down_good_count, metrics->valid_count);
    metrics->wvr_good_rate_per_mille =
        autocal_per_mille(metrics->wvr_good_count, metrics->valid_count);

    if (valid_end != 0U)
    {
        metrics->median_tof_up_ps =
            autocal_median_field(calibrator->samples, valid_end, AUTOCAL_FIELD_UP);
        metrics->median_tof_down_ps =
            autocal_median_field(calibrator->samples, valid_end, AUTOCAL_FIELD_DOWN);
        metrics->median_tof_diff_ps =
            autocal_median_field(calibrator->samples, valid_end, AUTOCAL_FIELD_DIFF);
        metrics->direction_delta_ps =
            autocal_abs_i64(metrics->median_tof_up_ps - metrics->median_tof_down_ps);

        metrics->mad_tof_up_ps = autocal_mad_field(
            calibrator->samples, valid_end, AUTOCAL_FIELD_UP, metrics->median_tof_up_ps);
        metrics->mad_tof_down_ps = autocal_mad_field(
            calibrator->samples, valid_end, AUTOCAL_FIELD_DOWN, metrics->median_tof_down_ps);
        metrics->mad_tof_diff_ps = autocal_mad_field(
            calibrator->samples, valid_end, AUTOCAL_FIELD_DIFF, metrics->median_tof_diff_ps);

        /*
         * A sample is classified as a cycle slip when either direction is more
         * than half one modeled acoustic period away from its batch median.
         * Half-period separation catches adjacent-wave lock without marking
         * normal sub-cycle jitter as a slip.
         */
        const int64_t expected_period_ps =
            (int64_t)(autocal_profile_dpl(&calibrator->candidate_profile) + 1U) *
            AUTOCAL_PS_PER_DPL_UNIT;
        const int64_t slip_threshold = expected_period_ps / 2;
        for (uint16_t i = 0U; i < valid_end; ++i)
        {
            if (autocal_abs_i64(calibrator->samples[i].tof_up_ps - metrics->median_tof_up_ps) >
                    slip_threshold ||
                autocal_abs_i64(calibrator->samples[i].tof_down_ps - metrics->median_tof_down_ps) >
                    slip_threshold)
            {
                metrics->cycle_slip_count++;
            }
        }

        /*
         * A valid TOF sample may still lack usable period evidence. Exclude
         * those samples from period median/MAD so their zero-initialized
         * period_error_ps cannot make a noisy candidate look artificially
         * good.
         */
        uint16_t wave_end = 0U;
        for (uint16_t i = 0U; i < valid_end; ++i)
        {
            if ((calibrator->samples[i].flags & MAX35103_AUTOCAL_SAMPLE_WAVE_VALID) != 0U)
            {
                if (i != wave_end)
                {
                    autocal_swap_samples(&calibrator->samples[i], &calibrator->samples[wave_end]);
                }
                wave_end++;
            }
        }
        if (wave_end != 0U)
        {
            metrics->median_period_error_ps =
                autocal_median_field(calibrator->samples, wave_end, AUTOCAL_FIELD_PERIOD);
            metrics->mad_period_error_ps = autocal_mad_field(calibrator->samples,
                                                             wave_end,
                                                             AUTOCAL_FIELD_PERIOD,
                                                             metrics->median_period_error_ps);
        }
    }
    metrics->cycle_slip_rate_per_mille =
        autocal_per_mille(metrics->cycle_slip_count, metrics->valid_count);

    /*
     * Gates retain their individual booleans for failure diagnosis. The
     * physical gate includes direction coherence and median window checks in
     * addition to the per-sample physical success rate.
     */
    metrics->communication_gate =
        metrics->valid_rate_per_mille >= calibrator->config.min_valid_rate_per_mille;
    metrics->direction_gate =
        metrics->valid_count != 0U &&
        metrics->direction_delta_ps <= calibrator->config.max_direction_delta_ps;
    metrics->physical_gate =
        metrics->valid_count != 0U && metrics->direction_gate &&
        metrics->physical_rate_per_mille >= metrics->physical_rate_required_per_mille &&
        metrics->median_tof_up_ps >= calibrator->config.expected_min_tof_ps &&
        metrics->median_tof_up_ps <= calibrator->config.expected_max_tof_ps &&
        metrics->median_tof_down_ps >= calibrator->config.expected_min_tof_ps &&
        metrics->median_tof_down_ps <= calibrator->config.expected_max_tof_ps;
    metrics->period_gate = autocal_period_gate(calibrator, metrics);
    metrics->waveform_gate =
        metrics->period_gate &&
        metrics->wvr_good_rate_per_mille >= calibrator->config.min_wvr_good_rate_per_mille;
    metrics->stage_waveform_gate = autocal_stage_waveform_gate(calibrator, metrics);
    metrics->statistics_gate =
        metrics->valid_count != 0U && metrics->mad_tof_up_ps <= calibrator->config.max_tof_mad_ps &&
        metrics->mad_tof_down_ps <= calibrator->config.max_tof_mad_ps &&
        metrics->mad_tof_diff_ps <= calibrator->config.max_diff_mad_ps &&
        metrics->cycle_slip_count <= calibrator->config.max_cycle_slips &&
        metrics->cycle_slip_rate_per_mille <= calibrator->config.max_cycle_slip_rate_per_mille;
    metrics->passed = metrics->communication_gate && metrics->physical_gate &&
                      metrics->waveform_gate && metrics->statistics_gate;

    /*
     * Weighted score priority, from strongest to weakest:
     *
     *   valid rate -> physical rate -> wave-evidence rate -> relevant WVR
     *   -> direction mismatch -> TOF dispersion -> period error -> cycle slips
     *
     * Saturation makes the ordering defined for extreme inputs. Explicit
     * physical and stage-waveform penalties keep failed-gate candidates behind
     * otherwise comparable passing candidates; eligibility is still decided
     * separately by boolean gates.
     */
    uint64_t score = 0U;
    score =
        autocal_sat_add_u64(score,
                            autocal_sat_mul_u64((uint64_t)(1000U - metrics->valid_rate_per_mille),
                                                UINT64_C(1000000000000)));
    score = autocal_sat_add_u64(
        score,
        autocal_sat_mul_u64((uint64_t)(1000U - metrics->physical_rate_per_mille),
                            UINT64_C(100000000000)));
    score = autocal_sat_add_u64(
        score,
        autocal_sat_mul_u64((uint64_t)(1000U - metrics->wave_valid_rate_per_mille),
                            UINT64_C(10000000000)));
    score = autocal_sat_add_u64(
        score,
        autocal_sat_mul_u64((uint64_t)(1000U - autocal_relevant_wvr_rate(calibrator, metrics)),
                            UINT64_C(1000000000)));
    score = autocal_sat_add_u64(
        score, autocal_sat_mul_u64((uint64_t)metrics->direction_delta_ps, UINT64_C(1000)));
    score = autocal_sat_add_u64(score, (uint64_t)metrics->mad_tof_up_ps);
    score = autocal_sat_add_u64(score, (uint64_t)metrics->mad_tof_down_ps);
    score = autocal_sat_add_u64(score, (uint64_t)metrics->mad_tof_diff_ps);
    score = autocal_sat_add_u64(score,
                                autocal_sat_mul_u64((uint64_t)metrics->median_period_error_ps, 4U));
    score = autocal_sat_add_u64(
        score, autocal_sat_mul_u64(metrics->cycle_slip_count, UINT64_C(100000000000)));
    if (!metrics->physical_gate)
    {
        score = autocal_sat_add_u64(score, UINT64_C(1000000000000000));
    }
    if (!metrics->stage_waveform_gate)
    {
        score = autocal_sat_add_u64(score, UINT64_C(1000000000000000));
    }
    metrics->score = score;
}

/**
 * @brief Return whether candidate metrics satisfy the current stage policy.
 *
 * Communication, physical, and stage-appropriate waveform evidence are always
 * mandatory. WAVE_SELECT and RETURN_UP/DOWN additionally require the full
 * statistics gate because those stages directly choose production receive
 * waves and comparator hysteresis before the expensive 128-sample VERIFY.
 */
static bool autocal_candidate_eligible(const Max35103AutoCalibrator *calibrator,
                                       const Max35103AutoCalMetrics *metrics)
{
    const bool base_gates = metrics->communication_gate && metrics->physical_gate &&
                            autocal_stage_waveform_gate(calibrator, metrics);
    if (!base_gates)
    {
        return false;
    }

    /*
     * The last search stages directly determine which wave is used in
     * production.  Do not let a short but visibly cycle-slipping batch reach
     * the 128-sample verify step.
     */
    switch (calibrator->state)
    {
    case MAX35103_AUTOCAL_STATE_WAVE_SELECT:
    case MAX35103_AUTOCAL_STATE_RETURN_UP:
    case MAX35103_AUTOCAL_STATE_RETURN_DOWN:
        return metrics->statistics_gate;

    default:
        return true;
    }
}

/**
 * @brief Insert a discovery candidate into the ordered finalist set.
 *
 * Finalists are kept in ascending score order. At most one entry is retained
 * for each complete TOF1 launch family, preventing small DLY variants of the
 * same PL/DPL/CT/polarity combination from consuming the entire fallback set.
 * A better member of an existing family replaces and reorders the old member.
 */
static void autocal_insert_discovery_finalist(Max35103AutoCalibrator *calibrator,
                                              const Max35103Profile *profile,
                                              const Max35103AutoCalMetrics *metrics)
{
    uint8_t count = calibrator->discovery_finalist_count;

    /*
     * Keep one best DLY point per launch family.  Four nearly identical DLY
     * values for the same PL/DPL/polarity would not be useful fallbacks.
     */
    for (uint8_t i = 0U; i < count; ++i)
    {
        if (profile->tof1 == calibrator->discovery_finalist_profiles[i].tof1)
        {
            if (metrics->score >= calibrator->discovery_finalist_metrics[i].score)
            {
                return;
            }
            for (uint8_t move = i; move + 1U < count; ++move)
            {
                calibrator->discovery_finalist_profiles[move] =
                    calibrator->discovery_finalist_profiles[move + 1U];
                calibrator->discovery_finalist_metrics[move] =
                    calibrator->discovery_finalist_metrics[move + 1U];
            }
            count--;
            break;
        }
    }

    uint8_t insert = count;
    for (uint8_t i = 0U; i < count; ++i)
    {
        if (metrics->score < calibrator->discovery_finalist_metrics[i].score)
        {
            insert = i;
            break;
        }
    }
    if (insert >= MAX35103_AUTOCAL_DISCOVERY_FINALISTS)
    {
        return;
    }

    if (count < MAX35103_AUTOCAL_DISCOVERY_FINALISTS)
    {
        count++;
    }
    for (uint8_t i = (uint8_t)(count - 1U); i > insert; --i)
    {
        calibrator->discovery_finalist_profiles[i] =
            calibrator->discovery_finalist_profiles[i - 1U];
        calibrator->discovery_finalist_metrics[i] = calibrator->discovery_finalist_metrics[i - 1U];
    }
    calibrator->discovery_finalist_profiles[insert] = *profile;
    calibrator->discovery_finalist_metrics[insert] = *metrics;
    calibrator->discovery_finalist_count = count;
}

/**
 * @brief Compare the evaluated candidate with the best and closest profiles.
 *
 * @c stage_closest tracks the lowest score regardless of eligibility and is
 * retained for failure diagnostics. @c stage_best tracks only eligible
 * candidates. ROBUSTNESS accumulates pass counts rather than selecting a
 * perturbed operating profile. VERIFY/RESET_VERIFY contain one candidate and
 * store its full-policy result directly.
 */
static void autocal_consider_candidate(Max35103AutoCalibrator *calibrator)
{
    autocal_finalize_metrics(calibrator, &calibrator->candidate_metrics);

    if (!calibrator->stage_closest_valid ||
        calibrator->candidate_metrics.score < calibrator->stage_closest_metrics.score)
    {
        calibrator->stage_closest_profile = calibrator->candidate_profile;
        calibrator->stage_closest_metrics = calibrator->candidate_metrics;
        calibrator->stage_closest_valid = true;
    }

    if (calibrator->state == MAX35103_AUTOCAL_STATE_ROBUSTNESS)
    {
        calibrator->perturbation_tested++;
        if (calibrator->candidate_metrics.passed)
        {
            calibrator->perturbation_passed++;
        }
        return;
    }

    if (calibrator->state == MAX35103_AUTOCAL_STATE_VERIFY ||
        calibrator->state == MAX35103_AUTOCAL_STATE_RESET_VERIFY)
    {
        calibrator->stage_best_profile = calibrator->candidate_profile;
        calibrator->stage_best_metrics = calibrator->candidate_metrics;
        calibrator->stage_best_valid = calibrator->candidate_metrics.passed;
        return;
    }

    if (!autocal_candidate_eligible(calibrator, &calibrator->candidate_metrics))
    {
        return;
    }
    if (calibrator->state == MAX35103_AUTOCAL_STATE_DISCOVERY)
    {
        autocal_insert_discovery_finalist(
            calibrator, &calibrator->candidate_profile, &calibrator->candidate_metrics);
    }
    if (!calibrator->stage_best_valid ||
        calibrator->candidate_metrics.score < calibrator->stage_best_metrics.score)
    {
        calibrator->stage_best_profile = calibrator->candidate_profile;
        calibrator->stage_best_metrics = calibrator->candidate_metrics;
        calibrator->stage_best_valid = true;
    }
}

/* -------------------------------------------------------------------------- */
/* Wave evidence normalization and sample classification                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Validate one pair of WVR Q1.7 ratios against configured limits.
 *
 * @param[in] config Q1.7 lower and upper bounds.
 * @param[in] t1_t2_q7 Raw unsigned Q1.7 t1/t2 ratio.
 * @param[in] t2_ideal_q7 Raw unsigned Q1.7 t2/tideal ratio.
 * @return true only when each ratio lies inside its own inclusive interval.
 */
static bool
autocal_wvr_pair_good(const Max35103AutoCalConfig *config, uint8_t t1_t2_q7, uint8_t t2_ideal_q7)
{
    return t1_t2_q7 >= config->wvr_t1_t2_min_q7 && t1_t2_q7 <= config->wvr_ratio_max_q7 &&
           t2_ideal_q7 >= config->wvr_t2_ideal_min_q7 && t2_ideal_q7 <= config->wvr_ratio_max_q7;
}

/**
 * @brief Estimate receive-wave period error from per-hit evidence.
 *
 * The expected adjacent-wave period is:
 *
 * @code
 * expected_period_ps = (DPL + 1) * 500000
 * @endcode
 *
 * For each adjacent HIT pair in each direction, the function accumulates the
 * absolute deviation from that period. @p mean_error_ps receives the mean
 * across both directions and all intervals.
 *
 * @return false when evidence is missing, fewer than two HITs are configured,
 *         or any adjacent timestamp is non-monotonic.
 */
static bool autocal_wave_period_error(const Max35103Profile *profile,
                                      const Max35103WaveEvidence *wave,
                                      int64_t *mean_error_ps)
{
    if (!profile || !wave || !mean_error_ps || !wave->valid || wave->configured_hit_count < 2U)
    {
        return false;
    }

    const int64_t expected_period_ps =
        (int64_t)(autocal_profile_dpl(profile) + 1U) * AUTOCAL_PS_PER_DPL_UNIT;
    uint64_t error_sum = 0U;
    uint16_t interval_count = 0U;

    for (uint8_t hit = 1U; hit < wave->configured_hit_count; ++hit)
    {
        const int64_t up_period = wave->hit_up_ps[hit] - wave->hit_up_ps[hit - 1U];
        const int64_t down_period = wave->hit_down_ps[hit] - wave->hit_down_ps[hit - 1U];
        if (up_period <= 0 || down_period <= 0)
        {
            return false;
        }
        error_sum += (uint64_t)autocal_abs_i64(up_period - expected_period_ps);
        error_sum += (uint64_t)autocal_abs_i64(down_period - expected_period_ps);
        interval_count = (uint16_t)(interval_count + 2U);
    }

    *mean_error_ps = (int64_t)(error_sum / (uint64_t)interval_count);
    return true;
}

/**
 * @brief Normalize hit timing to the estimated wave-zero acoustic arrival.
 *
 * For each configured HIT h:
 *
 * @code
 * wave_zero_h = hit_timestamp_h - programmed_wave_number_h * period
 * @endcode
 *
 * The returned UP and DOWN values are arithmetic means of these reconstructed
 * wave-zero estimates. This removes the intentional later-wave selection from
 * the physical path-arrival gate.
 *
 * @return false for invalid evidence, unsupported HIT count, or any
 *         non-positive reconstructed arrival.
 */
static bool autocal_wave_zero_tof(const Max35103Profile *profile,
                                  const Max35103WaveEvidence *wave,
                                  int64_t *tof_up_ps,
                                  int64_t *tof_down_ps)
{
    if (!profile || !wave || !tof_up_ps || !tof_down_ps || !wave->valid ||
        wave->configured_hit_count == 0U || wave->configured_hit_count > MAX35103_WAVE_HIT_COUNT)
    {
        return false;
    }

    const int64_t period_ps =
        (int64_t)(autocal_profile_dpl(profile) + 1U) * AUTOCAL_PS_PER_DPL_UNIT;
    int64_t up_sum_ps = 0;
    int64_t down_sum_ps = 0;

    for (uint8_t hit = 0U; hit < wave->configured_hit_count; ++hit)
    {
        const int64_t wave_delay_ps = (int64_t)autocal_profile_hit_wave(profile, hit) * period_ps;
        const int64_t up_ps = wave->hit_up_ps[hit] - wave_delay_ps;
        const int64_t down_ps = wave->hit_down_ps[hit] - wave_delay_ps;
        if (up_ps <= 0 || down_ps <= 0)
        {
            return false;
        }
        up_sum_ps += up_ps;
        down_sum_ps += down_ps;
    }

    *tof_up_ps = up_sum_ps / (int64_t)wave->configured_hit_count;
    *tof_down_ps = down_sum_ps / (int64_t)wave->configured_hit_count;
    return true;
}

/**
 * @brief Convert one driver measurement into a workspace sample and counters.
 *
 * Every caller-issued measurement attempt owns one zero-initialized workspace
 * slot. A timeout increments the candidate timeout counter. Non-OK, invalid
 * result, invalid wave evidence, or failed wave-zero reconstruction leaves the
 * slot unflagged so it affects attempted-rate gates but not timing statistics.
 *
 * A valid slot is independently classified for physical arrival, usable period
 * evidence, upstream WVR, downstream WVR, and bidirectional WVR.
 */
static void autocal_record_sample(Max35103AutoCalibrator *calibrator,
                                  Max35103Status status,
                                  const Max35103RawResult *result,
                                  const Max35103WaveEvidence *wave)
{
    Max35103AutoCalSample *sample = &calibrator->samples[calibrator->sample_index];
    memset(sample, 0, sizeof(*sample));

    if (status == MAX35103_TIMEOUT)
    {
        calibrator->candidate_metrics.timeout_count++;
    }
    /*
     * Result and wave validity must belong to the same backend measurement.
     * An unflagged slot still counts as an attempt when metrics are finalized.
     */
    if (status != MAX35103_OK || !result->valid || !wave->valid)
    {
        return;
    }

    calibrator->last_wvr_up_t1_t2_q7 = wave->wvr_up_t1_t2_q7;
    calibrator->last_wvr_up_t2_ideal_q7 = wave->wvr_up_t2_ideal_q7;
    calibrator->last_wvr_down_t1_t2_q7 = wave->wvr_down_t1_t2_q7;
    calibrator->last_wvr_down_t2_ideal_q7 = wave->wvr_down_t2_ideal_q7;

    /* Normalize selected HIT waves before applying the physical path window. */
    if (!autocal_wave_zero_tof(
            &calibrator->candidate_profile, wave, &sample->tof_up_ps, &sample->tof_down_ps))
    {
        return;
    }
    sample->tof_diff_ps = result->tof_diff_ps;
    sample->flags |= MAX35103_AUTOCAL_SAMPLE_VALID;

    if (sample->tof_up_ps >= calibrator->config.expected_min_tof_ps &&
        sample->tof_up_ps <= calibrator->config.expected_max_tof_ps &&
        sample->tof_down_ps >= calibrator->config.expected_min_tof_ps &&
        sample->tof_down_ps <= calibrator->config.expected_max_tof_ps)
    {
        sample->flags |= MAX35103_AUTOCAL_SAMPLE_PHYSICAL;
    }

    /* Period evidence is optional per sample and receives its own flag/rate. */
    if (autocal_wave_period_error(&calibrator->candidate_profile, wave, &sample->period_error_ps))
    {
        sample->flags |= MAX35103_AUTOCAL_SAMPLE_WAVE_VALID;
    }
    const bool wvr_up_good =
        autocal_wvr_pair_good(&calibrator->config, wave->wvr_up_t1_t2_q7, wave->wvr_up_t2_ideal_q7);
    const bool wvr_down_good = autocal_wvr_pair_good(
        &calibrator->config, wave->wvr_down_t1_t2_q7, wave->wvr_down_t2_ideal_q7);
    if (wvr_up_good)
    {
        sample->flags |= MAX35103_AUTOCAL_SAMPLE_WVR_UP_GOOD;
    }
    if (wvr_down_good)
    {
        sample->flags |= MAX35103_AUTOCAL_SAMPLE_WVR_DN_GOOD;
    }
    if (wvr_up_good && wvr_down_good)
    {
        sample->flags |= MAX35103_AUTOCAL_SAMPLE_WVR_GOOD;
    }
}

/* -------------------------------------------------------------------------- */
/* Failure, retry, fallback, and state transitions                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Return whether a driver status requires immediate search failure.
 *
 * SPI transport failure and NOT_READY are classified as fatal transport state
 * for consecutive-error accounting, although the caller still schedules reset
 * recovery until the configured consecutive-error budget is exhausted.
 */
static bool autocal_is_fatal_transport_status(Max35103Status status)
{
    return status == MAX35103_SPI_ERROR || status == MAX35103_NOT_READY;
}

/**
 * @brief Capture failure evidence and move the calibrator to FAILED.
 *
 * The function snapshots state/index/retry information before changing state.
 * If any candidate was scored in the stage, the closest profile and metrics are
 * retained; otherwise the active profile/metrics are used. This makes a
 * terminal report useful even when no candidate passed eligibility gates.
 */
static void autocal_fail(Max35103AutoCalibrator *calibrator, Max35103AutoCalStatus status)
{
    calibrator->failure_state = calibrator->state;
    calibrator->failure_candidate_index = calibrator->candidate_index;
    calibrator->failure_retry_count = calibrator->stage_retry_count;
    if (calibrator->stage_closest_valid)
    {
        calibrator->failure_profile = calibrator->stage_closest_profile;
        calibrator->failure_metrics = calibrator->stage_closest_metrics;
    }
    else
    {
        calibrator->failure_profile = calibrator->candidate_profile;
        calibrator->failure_metrics = calibrator->candidate_metrics;
    }
    calibrator->state = MAX35103_AUTOCAL_STATE_FAILED;
    calibrator->status = status;
    calibrator->candidate_configured = false;
}

/**
 * @brief Return the normal successor of a completed tuning stage.
 *
 * VERIFY and later states are handled explicitly by autocal_finish_stage() and
 * are therefore outside this linear tuning-stage mapping.
 */
static Max35103AutoCalState autocal_next_search_state(Max35103AutoCalState state)
{
    switch (state)
    {
    case MAX35103_AUTOCAL_STATE_DISCOVERY:
        return MAX35103_AUTOCAL_STATE_BIAS_CHARGE;
    case MAX35103_AUTOCAL_STATE_BIAS_CHARGE:
        return MAX35103_AUTOCAL_STATE_DLY_FINE;
    case MAX35103_AUTOCAL_STATE_DLY_FINE:
        return MAX35103_AUTOCAL_STATE_OFFSET_UP_COARSE;
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_COARSE:
        return MAX35103_AUTOCAL_STATE_OFFSET_UP_FINE;
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_FINE:
        return MAX35103_AUTOCAL_STATE_OFFSET_DOWN_COARSE;
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_COARSE:
        return MAX35103_AUTOCAL_STATE_OFFSET_DOWN_FINE;
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_FINE:
        return MAX35103_AUTOCAL_STATE_WAVE_SELECT;
    case MAX35103_AUTOCAL_STATE_WAVE_SELECT:
        return MAX35103_AUTOCAL_STATE_RETURN_UP;
    case MAX35103_AUTOCAL_STATE_RETURN_UP:
        return MAX35103_AUTOCAL_STATE_RETURN_DOWN;
    case MAX35103_AUTOCAL_STATE_RETURN_DOWN:
        return MAX35103_AUTOCAL_STATE_VERIFY;
    default:
        return MAX35103_AUTOCAL_STATE_FAILED;
    }
}

/**
 * @brief Restart tuning from the next discovery finalist when available.
 *
 * Fallback is disabled inside DISCOVERY itself. When allowed, it advances to
 * the next score-ordered, launch-family-distinct finalist, clears robustness
 * counters, and restarts the coordinate-tuning pipeline at BIAS_CHARGE.
 */
static bool autocal_try_profile_fallback(Max35103AutoCalibrator *calibrator)
{
    if (calibrator->state == MAX35103_AUTOCAL_STATE_DISCOVERY ||
        calibrator->profile_fallbacks_used >= calibrator->config.max_profile_fallbacks ||
        calibrator->discovery_finalist_index + 1U >= calibrator->discovery_finalist_count)
    {
        return false;
    }

    calibrator->discovery_finalist_index++;
    calibrator->profile_fallbacks_used++;
    calibrator->selected_profile =
        calibrator->discovery_finalist_profiles[calibrator->discovery_finalist_index];
    calibrator->perturbation_tested = 0U;
    calibrator->perturbation_passed = 0U;
    autocal_enter_state(calibrator, MAX35103_AUTOCAL_STATE_BIAS_CHARGE);
    return true;
}

/* -------------------------------------------------------------------------- */
/* Canonical evidence serialization and report publication                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Accumulate one byte into the report CRC-32.
 *
 * Uses the reflected CRC-32/ISO-HDLC polynomial 0xEDB88320. Initialization and
 * final XOR are performed by MAX35103_AutoCalReportCrc32().
 */
static void autocal_crc_byte(uint32_t *crc, uint8_t value)
{
    *crc ^= value;
    for (uint8_t bit = 0U; bit < 8U; ++bit)
    {
        *crc = (*crc & 1U) != 0U ? (*crc >> 1) ^ UINT32_C(0xEDB88320) : *crc >> 1;
    }
}

/**
 * @brief Accumulate a 16-bit value into the report CRC in canonical byte order.
 *
 * Least-significant byte is encoded first, independently of host endianness.
 */
static void autocal_crc_u16(uint32_t *crc, uint16_t value)
{
    autocal_crc_byte(crc, (uint8_t)value);
    autocal_crc_byte(crc, (uint8_t)(value >> 8));
}

/**
 * @brief Accumulate a 32-bit value into the report CRC in canonical byte order.
 */
static void autocal_crc_u32(uint32_t *crc, uint32_t value)
{
    for (uint8_t shift = 0U; shift < 32U; shift += 8U)
    {
        autocal_crc_byte(crc, (uint8_t)(value >> shift));
    }
}

/**
 * @brief Accumulate a 64-bit value into the report CRC in canonical byte order.
 */
static void autocal_crc_u64(uint32_t *crc, uint64_t value)
{
    for (uint8_t shift = 0U; shift < 64U; shift += 8U)
    {
        autocal_crc_byte(crc, (uint8_t)(value >> shift));
    }
}

/**
 * @brief Build the persistent evidence report from the selected profile.
 *
 * Called only after RESET_VERIFY passes and the selected profile has survived
 * reset/reconfiguration. The verification metrics copied here are therefore
 * the post-reset batch. zero_flow_offset_ps is always recorded as evidence, but
 * confidence is upgraded to ZERO_FLOW_COMPENSATED only when the application
 * externally asserted zero flow and TOF-difference MAD meets policy.
 */
static void autocal_finalize_report(Max35103AutoCalibrator *calibrator)
{
    memset(&calibrator->report, 0, sizeof(calibrator->report));
    calibrator->report.magic = MAX35103_AUTOCAL_REPORT_MAGIC;
    calibrator->report.report_version = MAX35103_AUTOCAL_REPORT_VERSION;
    calibrator->report.report_size = (uint16_t)sizeof(calibrator->report);
    calibrator->report.acoustic_path_length_um = calibrator->config.acoustic_path_length_um;
    calibrator->report.expected_min_tof_ps = calibrator->config.expected_min_tof_ps;
    calibrator->report.expected_max_tof_ps = calibrator->config.expected_max_tof_ps;
    calibrator->report.selected_profile = calibrator->selected_profile;
    calibrator->report.verification = calibrator->stage_best_metrics;
    calibrator->report.evaluated_candidate_count = calibrator->evaluated_candidate_count;
    calibrator->report.attempted_measurement_count = calibrator->attempted_measurement_count;
    calibrator->report.perturbation_tested = calibrator->perturbation_tested;
    calibrator->report.perturbation_passed = calibrator->perturbation_passed;
    calibrator->report.profile_fallbacks_used = calibrator->profile_fallbacks_used;
    calibrator->report.zero_flow_offset_ps = calibrator->stage_best_metrics.median_tof_diff_ps;
    calibrator->report.zero_flow_mad_ps = calibrator->stage_best_metrics.mad_tof_diff_ps;
    calibrator->report.reset_verified = true;
    calibrator->report.confidence = MAX35103_AUTOCAL_CONFIDENCE_ACOUSTIC_VERIFIED;

    if (calibrator->config.zero_flow_confirmed &&
        calibrator->stage_best_metrics.mad_tof_diff_ps <= calibrator->config.max_diff_mad_ps)
    {
        calibrator->report.confidence = MAX35103_AUTOCAL_CONFIDENCE_ZERO_FLOW_COMPENSATED;
    }
    calibrator->report.evidence_crc32 = MAX35103_AutoCalReportCrc32(&calibrator->report);
    calibrator->report_available = true;
}

/**
 * @brief Commit the current stage result and transition to the next stage.
 *
 * Transition policy:
 *
 * - ordinary tuning stage: retry, then finalist fallback, then failure if no
 *   eligible winner; otherwise commit winner and enter the next stage;
 * - VERIFY: require full pass, then test the fixed robustness neighborhood;
 * - ROBUSTNESS: require the configured number of passing perturbations, then
 *   enter RESET_VERIFY without adopting any perturbed profile;
 * - RESET_VERIFY: require a post-reset full pass, reapply the exact selected
 *   profile, finalize the report, and enter COMPLETE.
 */
static void autocal_finish_stage(Max35103AutoCalibrator *calibrator)
{
    if (calibrator->state == MAX35103_AUTOCAL_STATE_ROBUSTNESS)
    {
        /* Robustness judges the base profile's neighborhood, not a new winner. */
        if (calibrator->perturbation_passed < calibrator->config.required_perturbation_passes)
        {
            if (autocal_try_profile_fallback(calibrator))
            {
                return;
            }
            autocal_fail(calibrator, MAX35103_AUTOCAL_NO_CANDIDATE);
            return;
        }
        autocal_enter_state(calibrator, MAX35103_AUTOCAL_STATE_RESET_VERIFY);
        return;
    }

    if (calibrator->state == MAX35103_AUTOCAL_STATE_VERIFY)
    {
        /* A long independent batch must satisfy every final-policy gate. */
        if (!calibrator->stage_best_valid)
        {
            if (autocal_retry_stage_if_allowed(calibrator))
            {
                return;
            }
            if (autocal_try_profile_fallback(calibrator))
            {
                return;
            }
            autocal_fail(calibrator, MAX35103_AUTOCAL_NO_CANDIDATE);
            return;
        }
        calibrator->selected_profile = calibrator->stage_best_profile;
        calibrator->perturbation_tested = 0U;
        calibrator->perturbation_passed = 0U;
        autocal_enter_state(calibrator, MAX35103_AUTOCAL_STATE_ROBUSTNESS);
        return;
    }

    if (calibrator->state == MAX35103_AUTOCAL_STATE_RESET_VERIFY)
    {
        /* This is the final post-reset evidence gate and publication point. */
        if (!calibrator->stage_best_valid)
        {
            if (autocal_retry_stage_if_allowed(calibrator))
            {
                return;
            }
            if (autocal_try_profile_fallback(calibrator))
            {
                return;
            }
            autocal_fail(calibrator, MAX35103_AUTOCAL_NO_CANDIDATE);
            return;
        }
        calibrator->selected_profile = calibrator->stage_best_profile;
        /*
         * Leave hardware configured with the exact profile serialized in the
         * report. A failure here prevents publication even though the
         * measurement batch passed.
         */
        const Max35103Status status = calibrator->backend.configure(calibrator->backend.context,
                                                                    &calibrator->selected_profile);
        calibrator->last_driver_status = status;
        if (status != MAX35103_OK)
        {
            autocal_fail(calibrator, MAX35103_AUTOCAL_DRIVER_ERROR);
            return;
        }
        autocal_finalize_report(calibrator);
        calibrator->state = MAX35103_AUTOCAL_STATE_COMPLETE;
        calibrator->status = MAX35103_AUTOCAL_COMPLETE;
        return;
    }

    /* Ordinary coordinate-tuning stage: retry -> fallback -> fail. */
    if (!calibrator->stage_best_valid)
    {
        if (autocal_retry_stage_if_allowed(calibrator))
        {
            return;
        }
        if (autocal_try_profile_fallback(calibrator))
        {
            return;
        }
        autocal_fail(calibrator, MAX35103_AUTOCAL_NO_CANDIDATE);
        return;
    }

    calibrator->selected_profile = calibrator->stage_best_profile;
    if (calibrator->state == MAX35103_AUTOCAL_STATE_DISCOVERY)
    {
        calibrator->discovery_finalist_index = 0U;
        if (calibrator->discovery_finalist_count != 0U)
        {
            calibrator->selected_profile = calibrator->discovery_finalist_profiles[0];
        }
    }
    const Max35103AutoCalState next = autocal_next_search_state(calibrator->state);
    if (next == MAX35103_AUTOCAL_STATE_FAILED)
    {
        autocal_fail(calibrator, MAX35103_AUTOCAL_NO_CANDIDATE);
        return;
    }
    autocal_enter_state(calibrator, next);
}

/* -------------------------------------------------------------------------- */
/* Public configuration, lifecycle, and cooperative execution API             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Fill a conservative auto-calibration configuration for a water path.
 *
 * @param[out] config Complete derived policy.
 * @param[in] acoustic_path_length_um One-way acoustic distance, in um.
 * @param[in] transducer_frequency_hz Nominal transducer frequency, in Hz.
 *
 * @return MAX35103_AUTOCAL_OK on success.
 * @return MAX35103_AUTOCAL_INVALID_ARG for NULL/zero inputs.
 *
 * The arrival bounds are derived from distance divided by 1600 m/s and
 * 1400 m/s, then expanded by 1 us. DPL is estimated from the MAX35103's
 * 2 MHz/(DPL+1) launch relationship and searched one count around nominal.
 * DLY begins about 4 us before earliest arrival and ends with a 1 us guard.
 */
Max35103AutoCalStatus MAX35103_AutoCalDefaultConfig(Max35103AutoCalConfig *config,
                                                    uint32_t acoustic_path_length_um,
                                                    uint32_t transducer_frequency_hz)
{
    if (!config || acoustic_path_length_um == 0U || transducer_frequency_hz == 0U)
    {
        return MAX35103_AUTOCAL_INVALID_ARG;
    }

    memset(config, 0, sizeof(*config));
    config->acoustic_path_length_um = acoustic_path_length_um;

    /*
     * Unit derivation:
     *   distance_um * 1e6 / speed_m_per_s = one-way time_ps.
     */
    const uint64_t tof_fast_ps =
        (uint64_t)acoustic_path_length_um * UINT64_C(1000000) / AUTOCAL_WATER_FAST_MPS;
    const uint64_t tof_slow_ps =
        (uint64_t)acoustic_path_length_um * UINT64_C(1000000) / AUTOCAL_WATER_SLOW_MPS;
    config->expected_min_tof_ps = (int64_t)tof_fast_ps - AUTOCAL_TOF_MARGIN_PS;
    config->expected_max_tof_ps = (int64_t)tof_slow_ps + AUTOCAL_TOF_MARGIN_PS;
    if (config->expected_min_tof_ps < AUTOCAL_PS_PER_DLY_TICK)
    {
        config->expected_min_tof_ps = AUTOCAL_PS_PER_DLY_TICK;
    }

    /*
     * Round 2 MHz / f_transducer to the nearest integer divider, clamp to the
     * DPL-supported divider range 2..16, then encode DPL = divider - 1.
     */
    uint32_t divider = (UINT32_C(2000000) + transducer_frequency_hz / 2U) / transducer_frequency_hz;
    if (divider < 2U)
    {
        divider = 2U;
    }
    if (divider > 16U)
    {
        divider = 16U;
    }
    const uint8_t dpl = (uint8_t)(divider - 1U);
    config->dpl_min = dpl > 1U ? (uint8_t)(dpl - 1U) : 1U;
    config->dpl_max = dpl < 15U ? (uint8_t)(dpl + 1U) : 15U;
    config->pulse_count_min = 8U;
    config->pulse_count_max = 24U;
    config->pulse_count_step = 4U;
    config->ct_mask = 0x0FU;
    config->try_both_polarities = true;

    /*
     * DLY search remains before the earliest plausible direct arrival. The
     * minimum respects the driver's absolute register limit; the maximum keeps
     * a guard so receive timing does not start on top of the desired wave.
     */
    int64_t dly_min_ps = config->expected_min_tof_ps - AUTOCAL_DLY_LEAD_PS;
    if (dly_min_ps < (int64_t)MAX35103_TOF_DELAY_MIN * AUTOCAL_PS_PER_DLY_TICK)
    {
        dly_min_ps = (int64_t)MAX35103_TOF_DELAY_MIN * AUTOCAL_PS_PER_DLY_TICK;
    }
    int64_t dly_max_ps = config->expected_min_tof_ps - AUTOCAL_DLY_MAX_GUARD_PS;
    if (dly_max_ps < dly_min_ps)
    {
        dly_max_ps = dly_min_ps;
    }
    int64_t dly_min_ticks = dly_min_ps / AUTOCAL_PS_PER_DLY_TICK;
    int64_t dly_max_ticks = dly_max_ps / AUTOCAL_PS_PER_DLY_TICK;
    if (dly_min_ticks > UINT16_MAX)
    {
        dly_min_ticks = UINT16_MAX;
    }
    if (dly_max_ticks > UINT16_MAX)
    {
        dly_max_ticks = UINT16_MAX;
    }
    config->dly_min = (uint16_t)dly_min_ticks;
    config->dly_max = (uint16_t)dly_max_ticks;
    config->dly_coarse_step = 4U; /* 1 us */
    config->dly_fine_step = 1U;   /* 0.25 us */

    config->initial_offset_min = 0U;
    config->initial_offset_max = 32U;
    config->initial_offset_coarse_step = 4U;
    config->initial_offset_fine_step = 1U;
    config->return_offset_min = -16;
    config->return_offset_max = 16;
    config->return_offset_step = 4U;

    config->t2_wave_min = 2U;
    config->t2_wave_max = 10U;
    config->hit_count_min = 3U;
    config->hit_count_max = 6U;

    config->samples_per_candidate = 16U;
    config->finalist_samples = 32U;
    config->verification_samples = 128U;
    config->min_valid_rate_per_mille = 900U;
    /*
     * Search stages may accept 10/16 physical samples so later DLY, offset
     * and wave tuning can improve a marginal but correctly centered packet.
     * Discovery and final evidence use the stricter 800/1000 rate.
     */
    config->min_tuning_physical_rate_per_mille = 625U;
    config->min_physical_rate_per_mille = 800U;
    config->min_wave_valid_rate_per_mille = 900U;
    config->min_wvr_good_rate_per_mille = 750U;
    /*
     * The data sheet defines WVR[15:8] as t1/t2 and WVR[7:0] as
     * t2/tideal.  They are not interchangeable quality measures.  A narrow
     * early edge is expected on real pipe hardware, so accept any non-zero
     * t1/t2 result while retaining the 0.5 lower bound for t2/tideal.
     */
    config->wvr_t1_t2_min_q7 = 1U;
    config->wvr_t2_ideal_min_q7 = 64U; /* 0.5 */
    /* HIL T2/ideal is stable near 195; retain margin without going unbounded. */
    config->wvr_ratio_max_q7 = 208U; /* 1.625 */
    config->max_tof_mad_ps = INT64_C(250000);
    config->max_diff_mad_ps = INT64_C(50000);
    /*
     * MAX35103 HIT timestamps are quantized. Hardware measurements can land
     * just above 250 ns even though the UART's integer-ns diagnostic prints
     * "250". Keep enough margin for that boundary without admitting the
     * multi-microsecond false-period candidates seen during discovery.
     */
    config->max_period_error_ps = INT64_C(350000);
    /*
     * A normal flow-induced UP/DOWN difference is far below half an acoustic
     * period for the short water path.  A larger separation is much more
     * likely to be two directions locking to adjacent wave cycles.
     */
    config->max_direction_delta_ps =
        (int64_t)(UINT64_C(1000000000000) / (uint64_t)transducer_frequency_hz / UINT64_C(2));
    /*
     * Use the rate as the effective limit across 16-sample tuning and
     * 128-sample verification batches.  The absolute cap only prevents a
     * large verification batch from hiding excessive slips.
     */
    config->max_cycle_slips = 20U;
    config->max_cycle_slip_rate_per_mille = 150U;
    config->required_perturbation_passes = 6U;
    config->max_consecutive_driver_errors = 8U;
    config->max_stage_retries = 1U;
    config->max_profile_fallbacks = 3U;
    config->max_busy_polls = 1000U;
    config->zero_flow_confirmed = false;
    return MAX35103_AUTOCAL_OK;
}

/**
 * @brief Bind a MAX35103 driver to the hardware-independent backend interface.
 *
 * @param[in,out] driver Initialized, caller-owned driver.
 * @param[out] backend Callback table overwritten by this function.
 * @return MAX35103_AUTOCAL_OK or MAX35103_AUTOCAL_INVALID_ARG.
 *
 * The driver is borrowed as opaque context. No driver operation occurs here.
 */
Max35103AutoCalStatus MAX35103_AutoCalBindDriver(Max35103Driver *driver,
                                                 Max35103AutoCalBackend *backend)
{
    if (!driver || !backend)
    {
        return MAX35103_AUTOCAL_INVALID_ARG;
    }

    memset(backend, 0, sizeof(*backend));
    backend->configure = autocal_driver_configure;
    backend->measure = autocal_driver_measure;
    backend->reset = autocal_driver_reset;
    backend->context = driver;
    return MAX35103_AUTOCAL_OK;
}

/**
 * @brief Initialize a calibrator and bind its caller-owned sample workspace.
 *
 * @param[out] calibrator Instance cleared and initialized to IDLE.
 * @param[in] backend Callback table copied by value.
 * @param[in] config Search policy copied by value.
 * @param[in] seed_profile Valid complete profile copied by value.
 * @param[in,out] sample_workspace Borrowed sample array.
 * @param[in] sample_capacity Number of entries in @p sample_workspace.
 *
 * @return MAX35103_AUTOCAL_OK on success; otherwise
 *         MAX35103_AUTOCAL_INVALID_ARG.
 *
 * This function performs validation and memory binding only. It does not
 * configure, measure, or reset the device.
 */
Max35103AutoCalStatus MAX35103_AutoCalInit(Max35103AutoCalibrator *calibrator,
                                           const Max35103AutoCalBackend *backend,
                                           const Max35103AutoCalConfig *config,
                                           const Max35103Profile *seed_profile,
                                           Max35103AutoCalSample *sample_workspace,
                                           uint16_t sample_capacity)
{
    if (!calibrator || !autocal_backend_valid(backend) || !seed_profile || !sample_workspace ||
        !autocal_config_valid(config, sample_capacity) ||
        MAX35103_ValidateProfile(seed_profile) != MAX35103_OK)
    {
        return MAX35103_AUTOCAL_INVALID_ARG;
    }

    memset(calibrator, 0, sizeof(*calibrator));
    calibrator->backend = *backend;
    calibrator->config = *config;
    calibrator->seed_profile = *seed_profile;
    calibrator->selected_profile = *seed_profile;
    calibrator->samples = sample_workspace;
    calibrator->sample_capacity = sample_capacity;
    calibrator->state = MAX35103_AUTOCAL_STATE_IDLE;
    calibrator->status = MAX35103_AUTOCAL_OK;
    calibrator->last_driver_status = MAX35103_OK;
    return MAX35103_AUTOCAL_OK;
}

/**
 * @brief Reset runtime state and start a new profile search.
 *
 * Existing static configuration, backend binding, seed, and borrowed workspace
 * remain intact. All run-specific totals, finalists, failures, and report
 * publication state are cleared before entering DISCOVERY.
 */
Max35103AutoCalStatus MAX35103_AutoCalStart(Max35103AutoCalibrator *calibrator)
{
    if (!calibrator || !autocal_backend_valid(&calibrator->backend) ||
        !autocal_config_valid(&calibrator->config, calibrator->sample_capacity) ||
        !calibrator->samples)
    {
        return MAX35103_AUTOCAL_INVALID_ARG;
    }
    if (calibrator->state != MAX35103_AUTOCAL_STATE_IDLE &&
        calibrator->state != MAX35103_AUTOCAL_STATE_COMPLETE &&
        calibrator->state != MAX35103_AUTOCAL_STATE_FAILED &&
        calibrator->state != MAX35103_AUTOCAL_STATE_CANCELLED)
    {
        return MAX35103_AUTOCAL_RUNNING;
    }

    calibrator->selected_profile = calibrator->seed_profile;
    calibrator->evaluated_candidate_count = 0U;
    calibrator->attempted_measurement_count = 0U;
    calibrator->consecutive_driver_errors = 0U;
    calibrator->busy_poll_count = 0U;
    calibrator->perturbation_tested = 0U;
    calibrator->perturbation_passed = 0U;
    calibrator->discovery_finalist_count = 0U;
    calibrator->discovery_finalist_index = 0U;
    calibrator->profile_fallbacks_used = 0U;
    memset(calibrator->discovery_finalist_profiles,
           0,
           sizeof(calibrator->discovery_finalist_profiles));
    memset(
        calibrator->discovery_finalist_metrics, 0, sizeof(calibrator->discovery_finalist_metrics));
    calibrator->report_available = false;
    calibrator->recovery_required = false;
    memset(&calibrator->report, 0, sizeof(calibrator->report));
    calibrator->failure_state = MAX35103_AUTOCAL_STATE_IDLE;
    calibrator->failure_candidate_index = 0U;
    calibrator->failure_retry_count = 0U;
    memset(&calibrator->failure_profile, 0, sizeof(calibrator->failure_profile));
    memset(&calibrator->failure_metrics, 0, sizeof(calibrator->failure_metrics));
    autocal_enter_state(calibrator, MAX35103_AUTOCAL_STATE_DISCOVERY);
    calibrator->status = MAX35103_AUTOCAL_RUNNING;
    return calibrator->candidate_count == 0U ? MAX35103_AUTOCAL_INVALID_ARG
                                             : MAX35103_AUTOCAL_RUNNING;
}

/**
 * @brief Advance the search by at most one measurement or transition action.
 *
 * This is the cooperative scheduler for the complete algorithm. A successful
 * configure consumes one call and a measurement attempt consumes a later call.
 * MAX35103_BUSY consumes neither a candidate nor a sample, but repeated BUSY is
 * bounded by @c max_busy_polls.
 *
 * Transport/device errors mark recovery_required. The next call resets the
 * backend and forces the active candidate to be configured again before
 * measurement resumes.
 */
Max35103AutoCalStatus MAX35103_AutoCalStep(Max35103AutoCalibrator *calibrator)
{
    if (!calibrator)
    {
        return MAX35103_AUTOCAL_INVALID_ARG;
    }
    if (calibrator->state == MAX35103_AUTOCAL_STATE_COMPLETE)
    {
        return MAX35103_AUTOCAL_COMPLETE;
    }
    if (calibrator->state == MAX35103_AUTOCAL_STATE_FAILED ||
        calibrator->state == MAX35103_AUTOCAL_STATE_CANCELLED)
    {
        return calibrator->status;
    }
    if (calibrator->state == MAX35103_AUTOCAL_STATE_IDLE)
    {
        return MAX35103_AUTOCAL_OK;
    }

    /*
     * Recovery is deliberately its own bounded Step() action. A successful
     * reset invalidates device configuration, so candidate_configured is
     * cleared and the same candidate is reapplied on a later call.
     */
    if (calibrator->recovery_required)
    {
        const Max35103Status status = calibrator->backend.reset(calibrator->backend.context);
        calibrator->last_driver_status = status;
        if (status != MAX35103_OK)
        {
            if (calibrator->consecutive_driver_errors < UINT8_MAX)
            {
                calibrator->consecutive_driver_errors++;
            }
            if (calibrator->consecutive_driver_errors >=
                calibrator->config.max_consecutive_driver_errors)
            {
                autocal_fail(calibrator, MAX35103_AUTOCAL_DRIVER_ERROR);
                return calibrator->status;
            }
            return MAX35103_AUTOCAL_RUNNING;
        }
        calibrator->recovery_required = false;
        calibrator->candidate_configured = false;
        return MAX35103_AUTOCAL_RUNNING;
    }

    /*
     * RESET_VERIFY contains an intentional independent reset even when no error
     * occurred. This proves that the reported volatile profile can be restored
     * after device reset and still reproduces acceptable evidence.
     */
    if (calibrator->state == MAX35103_AUTOCAL_STATE_RESET_VERIFY && !calibrator->reset_performed)
    {
        const Max35103Status status = calibrator->backend.reset(calibrator->backend.context);
        calibrator->last_driver_status = status;
        if (status != MAX35103_OK)
        {
            if (calibrator->consecutive_driver_errors < UINT8_MAX)
            {
                calibrator->consecutive_driver_errors++;
            }
            if (calibrator->consecutive_driver_errors >=
                calibrator->config.max_consecutive_driver_errors)
            {
                autocal_fail(calibrator, MAX35103_AUTOCAL_DRIVER_ERROR);
                return calibrator->status;
            }
            return MAX35103_AUTOCAL_RUNNING;
        }
        calibrator->reset_performed = true;
        calibrator->candidate_configured = false;
        return MAX35103_AUTOCAL_RUNNING;
    }

    /* Candidate exhaustion is finalized as a separate transition-only action. */
    if (calibrator->candidate_index >= calibrator->candidate_count)
    {
        autocal_finish_stage(calibrator);
        return calibrator->status;
    }

    /*
     * Candidate preparation/configuration phase. Invalid generated profiles
     * are skipped deterministically and still count as evaluated candidate
     * slots; they do not consume hardware measurements.
     */
    if (!calibrator->candidate_configured)
    {
        if (!autocal_make_candidate(
                calibrator, calibrator->candidate_index, &calibrator->candidate_profile))
        {
            calibrator->candidate_started = false;
            calibrator->candidate_index++;
            calibrator->evaluated_candidate_count++;
            return MAX35103_AUTOCAL_RUNNING;
        }

        /*
         * Reset the batch only on the first configure attempt. If recovery
         * occurs mid-batch, already recorded attempts remain evidence for this
         * candidate and acquisition resumes after reconfiguration.
         */
        if (!calibrator->candidate_started)
        {
            calibrator->sample_index = 0U;
            calibrator->sample_target = autocal_sample_target(calibrator);
            memset(&calibrator->candidate_metrics, 0, sizeof(calibrator->candidate_metrics));
            calibrator->last_wvr_up_t1_t2_q7 = 0U;
            calibrator->last_wvr_up_t2_ideal_q7 = 0U;
            calibrator->last_wvr_down_t1_t2_q7 = 0U;
            calibrator->last_wvr_down_t2_ideal_q7 = 0U;
        }

        const Max35103Status status = calibrator->backend.configure(calibrator->backend.context,
                                                                    &calibrator->candidate_profile);
        calibrator->last_driver_status = status;
        /* BUSY is deferred work, not an invalid candidate or driver error. */
        if (status == MAX35103_BUSY)
        {
            if (calibrator->busy_poll_count < UINT16_MAX)
            {
                calibrator->busy_poll_count++;
            }
            if (calibrator->busy_poll_count >= calibrator->config.max_busy_polls)
            {
                autocal_fail(calibrator, MAX35103_AUTOCAL_DRIVER_ERROR);
                return calibrator->status;
            }
            return MAX35103_AUTOCAL_RUNNING;
        }
        calibrator->busy_poll_count = 0U;
        if (status != MAX35103_OK)
        {
            /*
             * Candidate-local configuration rejection skips only this
             * candidate. Other errors are treated as recoverable backend state
             * until the consecutive-error budget is exhausted.
             */
            if (status == MAX35103_CONFIG_ERROR || status == MAX35103_INVALID_ARG)
            {
                calibrator->candidate_started = false;
                calibrator->candidate_index++;
                calibrator->evaluated_candidate_count++;
                return MAX35103_AUTOCAL_RUNNING;
            }
            if (calibrator->consecutive_driver_errors < UINT8_MAX)
            {
                calibrator->consecutive_driver_errors++;
            }
            if (calibrator->consecutive_driver_errors >=
                calibrator->config.max_consecutive_driver_errors)
            {
                autocal_fail(calibrator, MAX35103_AUTOCAL_DRIVER_ERROR);
                return calibrator->status;
            }
            calibrator->recovery_required = true;
            return MAX35103_AUTOCAL_RUNNING;
        }

        calibrator->consecutive_driver_errors = 0U;
        if (!calibrator->candidate_started)
        {
            calibrator->candidate_started = true;
        }
        calibrator->candidate_configured = true;
        return MAX35103_AUTOCAL_RUNNING;
    }

    /* Candidate is configured: issue exactly one measurement attempt. */
    Max35103RawResult result;
    Max35103WaveEvidence wave;
    memset(&result, 0, sizeof(result));
    memset(&wave, 0, sizeof(wave));
    const Max35103Status status =
        calibrator->backend.measure(calibrator->backend.context, &result, &wave);
    calibrator->last_driver_status = status;
    if (status == MAX35103_BUSY)
    {
        if (calibrator->busy_poll_count < UINT16_MAX)
        {
            calibrator->busy_poll_count++;
        }
        if (calibrator->busy_poll_count >= calibrator->config.max_busy_polls)
        {
            autocal_fail(calibrator, MAX35103_AUTOCAL_DRIVER_ERROR);
            return calibrator->status;
        }
        return MAX35103_AUTOCAL_RUNNING;
    }
    calibrator->busy_poll_count = 0U;
    /*
     * Every non-BUSY call is a completed attempt for rate accounting, even if
     * its sample remains invalid because of timeout or bad acoustic evidence.
     */
    autocal_record_sample(calibrator, status, &result, &wave);
    calibrator->sample_index++;
    calibrator->attempted_measurement_count++;

    if (autocal_is_fatal_transport_status(status))
    {
        if (calibrator->consecutive_driver_errors < UINT8_MAX)
        {
            calibrator->consecutive_driver_errors++;
        }
        calibrator->recovery_required = true;
        if (calibrator->consecutive_driver_errors >=
            calibrator->config.max_consecutive_driver_errors)
        {
            autocal_fail(calibrator, MAX35103_AUTOCAL_DRIVER_ERROR);
            return calibrator->status;
        }
    }
    else
    {
        calibrator->consecutive_driver_errors = 0U;
        if (status == MAX35103_TIMEOUT || status == MAX35103_DEVICE_ERROR ||
            status == MAX35103_NOT_READY)
        {
            calibrator->recovery_required = true;
        }
    }

    /* Finalize and rank only after the configured batch size is complete. */
    if (calibrator->sample_index >= calibrator->sample_target)
    {
        autocal_consider_candidate(calibrator);
        calibrator->candidate_index++;
        calibrator->evaluated_candidate_count++;
        calibrator->candidate_started = false;
        calibrator->candidate_configured = false;
    }
    return MAX35103_AUTOCAL_RUNNING;
}

/**
 * @brief Cancel an active search and preserve its diagnostic state.
 *
 * Cancellation modifies software state only. It does not issue HALT/reset,
 * reapply the seed profile, or publish a partial report.
 */
void MAX35103_AutoCalCancel(Max35103AutoCalibrator *calibrator)
{
    if (!calibrator || calibrator->state == MAX35103_AUTOCAL_STATE_COMPLETE ||
        calibrator->state == MAX35103_AUTOCAL_STATE_FAILED)
    {
        return;
    }
    calibrator->state = MAX35103_AUTOCAL_STATE_CANCELLED;
    calibrator->status = MAX35103_AUTOCAL_CANCELLED;
    calibrator->candidate_configured = false;
}

/**
 * @brief Return the current auto-calibration state.
 *
 * A NULL pointer maps to FAILED so diagnostic callers receive a conservative
 * state without dereferencing invalid memory.
 */
Max35103AutoCalState MAX35103_AutoCalGetState(const Max35103AutoCalibrator *calibrator)
{
    return calibrator ? calibrator->state : MAX35103_AUTOCAL_STATE_FAILED;
}

/**
 * @brief Copy a consistent progress snapshot to caller-owned storage.
 *
 * The function zero-initializes the destination before copying fields. "Consistent"
 * means all fields come from one call in the serialized owner context; no
 * lock or atomic snapshot is provided for concurrent callers.
 */
void MAX35103_AutoCalGetProgress(const Max35103AutoCalibrator *calibrator,
                                 Max35103AutoCalProgress *progress)
{
    if (!progress)
    {
        return;
    }
    memset(progress, 0, sizeof(*progress));
    if (!calibrator)
    {
        progress->state = MAX35103_AUTOCAL_STATE_FAILED;
        progress->last_driver_status = MAX35103_INVALID_ARG;
        return;
    }
    progress->state = calibrator->state;
    progress->candidate_index = calibrator->candidate_index;
    progress->candidate_count = calibrator->candidate_count;
    progress->sample_index = calibrator->sample_index;
    progress->sample_target = calibrator->sample_target;
    progress->stage_retry_count = calibrator->stage_retry_count;
    progress->discovery_finalist_index = calibrator->discovery_finalist_index;
    progress->discovery_finalist_count = calibrator->discovery_finalist_count;
    progress->profile_fallbacks_used = calibrator->profile_fallbacks_used;
    progress->evaluated_candidate_count = calibrator->evaluated_candidate_count;
    progress->attempted_measurement_count = calibrator->attempted_measurement_count;
    progress->last_driver_status = calibrator->last_driver_status;
}

/**
 * @brief Return whether a completed evidence report is available.
 *
 * Availability is set only by autocal_finalize_report() after RESET_VERIFY.
 */
bool MAX35103_AutoCalHasReport(const Max35103AutoCalibrator *calibrator)
{
    return calibrator && calibrator->report_available;
}

/**
 * @brief Copy the completed evidence report to caller-owned storage.
 *
 * The caller receives a value copy and may persist it independently of the
 * calibrator. A running status here means simply "no published report yet";
 * inspect state/status separately for terminal failure details.
 */
Max35103AutoCalStatus MAX35103_AutoCalGetReport(const Max35103AutoCalibrator *calibrator,
                                                Max35103AutoCalReport *report)
{
    if (!calibrator || !report)
    {
        return MAX35103_AUTOCAL_INVALID_ARG;
    }
    if (!calibrator->report_available)
    {
        return MAX35103_AUTOCAL_RUNNING;
    }
    *report = calibrator->report;
    return MAX35103_AUTOCAL_COMPLETE;
}

/**
 * @brief Return a stable diagnostic string for an auto-calibration state.
 *
 * Strings are compile-time literals suitable for UART logs. Their spelling is
 * part of the diagnostic interface and is independent of enum numeric values.
 */
const char *MAX35103_AutoCalStateName(Max35103AutoCalState state)
{
    switch (state)
    {
    case MAX35103_AUTOCAL_STATE_IDLE:
        return "IDLE";
    case MAX35103_AUTOCAL_STATE_DISCOVERY:
        return "DISCOVERY";
    case MAX35103_AUTOCAL_STATE_BIAS_CHARGE:
        return "BIAS_CHARGE";
    case MAX35103_AUTOCAL_STATE_DLY_FINE:
        return "DLY_FINE";
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_COARSE:
        return "OFFSET_UP_COARSE";
    case MAX35103_AUTOCAL_STATE_OFFSET_UP_FINE:
        return "OFFSET_UP_FINE";
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_COARSE:
        return "OFFSET_DOWN_COARSE";
    case MAX35103_AUTOCAL_STATE_OFFSET_DOWN_FINE:
        return "OFFSET_DOWN_FINE";
    case MAX35103_AUTOCAL_STATE_WAVE_SELECT:
        return "WAVE_SELECT";
    case MAX35103_AUTOCAL_STATE_RETURN_UP:
        return "RETURN_UP";
    case MAX35103_AUTOCAL_STATE_RETURN_DOWN:
        return "RETURN_DOWN";
    case MAX35103_AUTOCAL_STATE_VERIFY:
        return "VERIFY";
    case MAX35103_AUTOCAL_STATE_ROBUSTNESS:
        return "ROBUSTNESS";
    case MAX35103_AUTOCAL_STATE_RESET_VERIFY:
        return "RESET_VERIFY";
    case MAX35103_AUTOCAL_STATE_COMPLETE:
        return "COMPLETE";
    case MAX35103_AUTOCAL_STATE_FAILED:
        return "FAILED";
    case MAX35103_AUTOCAL_STATE_CANCELLED:
        return "CANCELLED";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Calculate the canonical CRC-32 of report evidence fields.
 *
 * @param[in] report Evidence report to serialize logically.
 * @return CRC-32/ISO-HDLC, or zero for NULL.
 *
 * Fields are fed explicitly in schema order and little-endian canonical byte
 * order. Structure padding and @c evidence_crc32 are excluded. Signed timing
 * values are encoded through their uint64_t two's-complement bit pattern.
 */
uint32_t MAX35103_AutoCalReportCrc32(const Max35103AutoCalReport *report)
{
    if (!report)
    {
        return 0U;
    }

    uint32_t crc = UINT32_C(0xFFFFFFFF);
    autocal_crc_u32(&crc, report->magic);
    autocal_crc_u16(&crc, report->report_version);
    autocal_crc_u16(&crc, report->report_size);
    autocal_crc_u32(&crc, report->acoustic_path_length_um);
    autocal_crc_u64(&crc, (uint64_t)report->expected_min_tof_ps);
    autocal_crc_u64(&crc, (uint64_t)report->expected_max_tof_ps);

    const Max35103Profile *profile = &report->selected_profile;
    autocal_crc_u32(&crc, profile->profile_id);
    autocal_crc_u32(&crc, profile->profile_version);
    autocal_crc_byte(&crc, profile->event_mode_cmd);
    autocal_crc_u16(&crc, profile->tof1);
    autocal_crc_u16(&crc, profile->tof2);
    autocal_crc_u16(&crc, profile->tof3);
    autocal_crc_u16(&crc, profile->tof4);
    autocal_crc_u16(&crc, profile->tof5);
    autocal_crc_u16(&crc, profile->tof6);
    autocal_crc_u16(&crc, profile->tof7);
    autocal_crc_u16(&crc, profile->event_timing_1);
    autocal_crc_u16(&crc, profile->event_timing_2);
    autocal_crc_u16(&crc, profile->tof_measurement_delay);
    autocal_crc_u16(&crc, profile->calibration_control);
    autocal_crc_u32(&crc, profile->init_timeout_ms);
    autocal_crc_u32(&crc, profile->result_timeout_ms);
    autocal_crc_u32(&crc, profile->halt_timeout_ms);
    autocal_crc_u32(&crc, profile->reference_resistance_milliohm);
    autocal_crc_u32(&crc, profile->rtd_nominal_resistance_milliohm);

    const Max35103AutoCalMetrics *metrics = &report->verification;
    autocal_crc_u16(&crc, metrics->attempted_count);
    autocal_crc_u16(&crc, metrics->valid_count);
    autocal_crc_u16(&crc, metrics->physical_count);
    autocal_crc_u16(&crc, metrics->wave_valid_count);
    autocal_crc_u16(&crc, metrics->wvr_up_good_count);
    autocal_crc_u16(&crc, metrics->wvr_down_good_count);
    autocal_crc_u16(&crc, metrics->wvr_good_count);
    autocal_crc_u16(&crc, metrics->timeout_count);
    autocal_crc_u16(&crc, metrics->cycle_slip_count);
    autocal_crc_u16(&crc, metrics->valid_rate_per_mille);
    autocal_crc_u16(&crc, metrics->physical_rate_per_mille);
    autocal_crc_u16(&crc, metrics->physical_rate_required_per_mille);
    autocal_crc_u16(&crc, metrics->wave_valid_rate_per_mille);
    autocal_crc_u16(&crc, metrics->wvr_up_good_rate_per_mille);
    autocal_crc_u16(&crc, metrics->wvr_down_good_rate_per_mille);
    autocal_crc_u16(&crc, metrics->wvr_good_rate_per_mille);
    autocal_crc_u16(&crc, metrics->cycle_slip_rate_per_mille);
    autocal_crc_u64(&crc, (uint64_t)metrics->median_tof_up_ps);
    autocal_crc_u64(&crc, (uint64_t)metrics->median_tof_down_ps);
    autocal_crc_u64(&crc, (uint64_t)metrics->median_tof_diff_ps);
    autocal_crc_u64(&crc, (uint64_t)metrics->direction_delta_ps);
    autocal_crc_u64(&crc, (uint64_t)metrics->mad_tof_up_ps);
    autocal_crc_u64(&crc, (uint64_t)metrics->mad_tof_down_ps);
    autocal_crc_u64(&crc, (uint64_t)metrics->mad_tof_diff_ps);
    autocal_crc_u64(&crc, (uint64_t)metrics->median_period_error_ps);
    autocal_crc_u64(&crc, (uint64_t)metrics->mad_period_error_ps);
    autocal_crc_u64(&crc, metrics->score);
    autocal_crc_byte(&crc, metrics->communication_gate ? 1U : 0U);
    autocal_crc_byte(&crc, metrics->direction_gate ? 1U : 0U);
    autocal_crc_byte(&crc, metrics->physical_gate ? 1U : 0U);
    autocal_crc_byte(&crc, metrics->period_gate ? 1U : 0U);
    autocal_crc_byte(&crc, metrics->waveform_gate ? 1U : 0U);
    autocal_crc_byte(&crc, metrics->stage_waveform_gate ? 1U : 0U);
    autocal_crc_byte(&crc, metrics->statistics_gate ? 1U : 0U);
    autocal_crc_byte(&crc, metrics->passed ? 1U : 0U);

    autocal_crc_u32(&crc, report->evaluated_candidate_count);
    autocal_crc_u32(&crc, report->attempted_measurement_count);
    autocal_crc_u16(&crc, report->perturbation_tested);
    autocal_crc_u16(&crc, report->perturbation_passed);
    autocal_crc_byte(&crc, report->profile_fallbacks_used);
    autocal_crc_u64(&crc, (uint64_t)report->zero_flow_offset_ps);
    autocal_crc_u64(&crc, (uint64_t)report->zero_flow_mad_ps);
    autocal_crc_u32(&crc, (uint32_t)report->confidence);
    autocal_crc_byte(&crc, report->reset_verified ? 1U : 0U);
    return crc ^ UINT32_C(0xFFFFFFFF);
}

/**
 * @brief Run the complete auto-calibration workflow using a local workspace.
 *
 * This internal convenience entry point constructs default policy, binds the
 * supplied driver, selects a caller seed or built-in seed, and spins Step()
 * until terminal state.
 *
 * @warning The function uses a static sample workspace and is not reentrant.
 * @warning The polling loop provides no delay, watchdog feed, cancellation,
 *          logging, or RTOS yield. Embedded applications should normally use
 *          the cooperative Init()/Start()/Step() API through their board layer.
 *
 * @return MAX35103_AUTOCAL_OK when calibration completes and outputs are
 *         copied; otherwise the first setup or terminal search error.
 */
Max35103AutoCalStatus MAX35103_AutoCal(Max35103Driver *driver,
                                       uint32_t acoustic_path_um,
                                       uint32_t transducer_freq_hz,
                                       const Max35103Profile *seed_profile,
                                       Max35103Profile *out_profile,
                                       Max35103AutoCalReport *out_report)
{
    Max35103AutoCalStatus status;

    /* 1. Validate mandatory driver and selected-profile output. */
    if ((driver == NULL) || (out_profile == NULL))
    {
        return MAX35103_AUTOCAL_INVALID_ARG;
    }

    /* 2. Generate complete policy from physical path and transducer inputs. */
    Max35103AutoCalConfig config;
    status = MAX35103_AutoCalDefaultConfig(&config, acoustic_path_um, transducer_freq_hz);
    if (status != MAX35103_AUTOCAL_OK)
    {
        return status;
    }

    /* 3. Bind the normal driver to the portable callback contract. */
    Max35103AutoCalBackend backend;
    status = MAX35103_AutoCalBindDriver(driver, &backend);
    if (status != MAX35103_AUTOCAL_OK)
    {
        return status;
    }

    /* 4. Static workspace: no heap, but shared and therefore not reentrant. */
    static Max35103AutoCalSample s_workspace[MAX35103_AUTOCAL_SAMPLE_WORKSPACE_SIZE];

    /* 5. Use caller seed when supplied; otherwise use the built-in volatile seed. */
    static const Max35103Profile s_default_seed = MAX35103_AUTOCAL_SEED_DEFAULT;
    const Max35103Profile *effective_seed = (seed_profile != NULL) ? seed_profile : &s_default_seed;

    /* 6. Validate and initialize the stack-owned calibrator instance. */
    Max35103AutoCalibrator calibrator;
    status = MAX35103_AutoCalInit(&calibrator,
                                  &backend,
                                  &config,
                                  effective_seed,
                                  s_workspace,
                                  MAX35103_AUTOCAL_SAMPLE_WORKSPACE_SIZE);
    if (status != MAX35103_AUTOCAL_OK)
    {
        return status;
    }

    /* 7. Clear runtime evidence and enter DISCOVERY. */
    status = MAX35103_AutoCalStart(&calibrator);
    if (status != MAX35103_AUTOCAL_RUNNING)
    {
        return status;
    }

    /* 8. Poll synchronously until a terminal state is reached. */
    while ((status = MAX35103_AutoCalStep(&calibrator)) == MAX35103_AUTOCAL_RUNNING)
    {
        /* No watchdog feed, delay, or logging: this is a pure blocking spin. */
    }

    /* 9. Copy the exact selected profile and optional persistent evidence. */
    if (status == MAX35103_AUTOCAL_COMPLETE)
    {
        *out_profile = calibrator.selected_profile;
        if (out_report != NULL)
        {
            status = MAX35103_AutoCalGetReport(&calibrator, out_report);
            if (status != MAX35103_AUTOCAL_COMPLETE)
            {
                return status;
            }
        }
        return MAX35103_AUTOCAL_OK;
    }

    /* Propagate any other terminal status directly as an error. */
    return status;
}