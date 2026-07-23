/**
  ******************************************************************************
  * @file    max35103_calibrate.c
  * @brief   MAX35103 AutoCal board integration
  *
  * Extracted from main.c to keep the application entry point lean.
  * See max35103_calibrate.h for the public API.
  ******************************************************************************
  */

#include "max35103_calibrate.h"
#include "max35103_stm32_hal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Internal constants
 * ------------------------------------------------------------------------- */

#define CAL_SAMPLE_CAPACITY           128U
#define CAL_LOG_BUFFER_SIZE           512U
#define CAL_LOG_CANDIDATE_INTERVAL    50U
#define CAL_DIAG_CANDIDATE_INTERVAL   10U
#define CAL_ACOUSTIC_PATH_UM          15000U
#define CAL_TRANSDUCER_FREQUENCY_HZ   1000000U

/* ---------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

static Max35103Driver              s_driver;
static Max35103AutoCalBackend      s_autocal_backend;
static Max35103AutoCalibrator      s_autocal;
static Max35103AutoCalSample
    s_autocal_samples[CAL_SAMPLE_CAPACITY];
static Max35103AutoCalReport       s_autocal_report;
static Max35103Stm32HalContext     s_hal_context;
static Max35103Transport           s_transport;
static UART_HandleTypeDef         *s_huart;

static bool                        s_active;
static Max35103AutoCalState        s_last_logged_state =
    MAX35103_AUTOCAL_STATE_IDLE;
static uint32_t                    s_last_logged_candidate;
static uint32_t                    s_last_diagnostic_candidate;
static uint8_t                     s_last_logged_retry;
static uint8_t                     s_last_logged_fallback;

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static void cal_log(const char *format, ...)
{
    char buffer[CAL_LOG_BUFFER_SIZE];
    va_list args;

    if (s_huart == NULL) {
        return;
    }

    va_start(args, format);
    const int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (length <= 0) {
        return;
    }

    const uint16_t tx_len =
        (length < (int)sizeof(buffer))
            ? (uint16_t)length
            : (uint16_t)(sizeof(buffer) - 1U);

    (void)HAL_UART_Transmit(s_huart, (uint8_t *)buffer,
                            tx_len, HAL_MAX_DELAY);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

Max35103AutoCalStatus MAX35103_Calibrate_Init(
    SPI_HandleTypeDef *hspi,
    GPIO_TypeDef *nss_port, uint16_t nss_pin,
    GPIO_TypeDef *reset_port, uint16_t reset_pin,
    UART_HandleTypeDef *huart,
    const Max35103Profile *profile)
{
    /* Decide which profile to use. */
    static const Max35103Profile k_default_profile = {
        .profile_id = 1U,
        .profile_version = 1U,
        .event_mode_cmd = MAX35103_CMD_EVTMG2,
        .tof1 = 0x1210U,
        .tof2 = 0x0001U,
        .tof3 = 0x0002U,
        .tof4 = 0x0003U,
        .tof5 = 0x0004U,
        .tof6 = 0x0005U,
        .tof7 = 0x0006U,
        .event_timing_1 = 0x0100U,
        .event_timing_2 = MAX35103_EVT2_TEMP_T1_T3,
        .tof_measurement_delay = 0x0020U,
        .calibration_control = MAX35103_CAL_CTRL_INT_EN,
        .init_timeout_ms = 20U,
        .result_timeout_ms = 20U,
        .halt_timeout_ms = 20U,
        .reference_resistance_milliohm = 1000000U,
        .rtd_nominal_resistance_milliohm = 100000U,
    };

    if (profile == NULL) {
        profile = &k_default_profile;
    }

    s_huart = huart;

    /* ── Transport ──────────────────────────────────────────────────── */
    Max35103Status driver_status = MAX35103_Stm32HalInitTransport(
        &s_hal_context, hspi,
        nss_port, nss_pin, reset_port, reset_pin,
        &s_transport);
    if (driver_status != MAX35103_OK) {
        cal_log("AUTOCAL|START_FAIL|stage=transport|driver_status=%d\r\n",
                (int)driver_status);
        return MAX35103_AUTOCAL_DRIVER_ERROR;
    }

    /* ── Driver ─────────────────────────────────────────────────────── */
    driver_status = MAX35103_Init(&s_driver, &s_transport);
    if (driver_status != MAX35103_OK) {
        cal_log("AUTOCAL|START_FAIL|stage=driver_init|driver_status=%d\r\n",
                (int)driver_status);
        return MAX35103_AUTOCAL_DRIVER_ERROR;
    }

    driver_status = MAX35103_ResetDevice(&s_driver);
    if (driver_status != MAX35103_OK) {
        cal_log("AUTOCAL|START_FAIL|stage=device_reset|driver_status=%d\r\n",
                (int)driver_status);
        return MAX35103_AUTOCAL_DRIVER_ERROR;
    }

    /* ── AutoCal configuration ──────────────────────────────────────── */
    Max35103AutoCalConfig config;
    Max35103AutoCalStatus status = MAX35103_AutoCalDefaultConfig(
        &config, CAL_ACOUSTIC_PATH_UM, CAL_TRANSDUCER_FREQUENCY_HZ);
    if (status != MAX35103_AUTOCAL_OK) {
        cal_log("AUTOCAL|START_FAIL|stage=default_config|status=%d\r\n",
                (int)status);
        return status;
    }

    /*
     * HIL tuning for the 15 mm acoustic path.  Keep the receiver
     * disabled until 7 us, then sweep across the leading edge of the
     * expected 8.375..11.714 us direct-arrival window.
     */
    config.dpl_min = 1U;
    config.dpl_max = 1U;
    config.dly_min = 0x001CU;   /* 7.00 us */
    config.dly_max = 0x0023U;   /* 8.75 us */
    config.dly_coarse_step = 1U;
    config.dly_fine_step = 1U;
    config.zero_flow_confirmed = true;

    status = MAX35103_AutoCalBindDriver(&s_driver, &s_autocal_backend);
    if (status != MAX35103_AUTOCAL_OK) {
        cal_log("AUTOCAL|START_FAIL|stage=bind_driver|status=%d\r\n",
                (int)status);
        return status;
    }

    status = MAX35103_AutoCalInit(
        &s_autocal, &s_autocal_backend, &config, profile,
        s_autocal_samples,
        (uint16_t)(sizeof(s_autocal_samples) /
                   sizeof(s_autocal_samples[0])));
    if (status != MAX35103_AUTOCAL_OK) {
        cal_log("AUTOCAL|START_FAIL|stage=autocal_init|status=%d\r\n",
                (int)status);
        return status;
    }

    s_last_logged_state      = MAX35103_AUTOCAL_STATE_IDLE;
    s_last_logged_candidate  = 0U;
    s_last_diagnostic_candidate = 0U;
    s_last_logged_retry      = 0U;
    s_last_logged_fallback   = 0U;

    status = MAX35103_AutoCalStart(&s_autocal);
    if (status != MAX35103_AUTOCAL_RUNNING) {
        cal_log("AUTOCAL|START_FAIL|stage=autocal_start|status=%d\r\n",
                (int)status);
        return status;
    }

    s_active = true;

    /* ── START log ──────────────────────────────────────────────────── */
    cal_log(
        "AUTOCAL|START|path_um=%lu|arrival_ns=%ld..%ld"
        "|dly_ticks=%u..%u|dpl=%u..%u|zero_flow=%u\r\n",
        (unsigned long)s_autocal.config.acoustic_path_length_um,
        (long)(s_autocal.config.expected_min_tof_ps / INT64_C(1000)),
        (long)(s_autocal.config.expected_max_tof_ps / INT64_C(1000)),
        (unsigned)s_autocal.config.dly_min,
        (unsigned)s_autocal.config.dly_max,
        (unsigned)s_autocal.config.dpl_min,
        (unsigned)s_autocal.config.dpl_max,
        (unsigned)(s_autocal.config.zero_flow_confirmed ? 1U : 0U));

    /* ── POLICY log ─────────────────────────────────────────────────── */
    cal_log(
        "AUTOCAL|POLICY|valid_rate=%u"
        "|physical_tuning=%u|physical_verify=%u"
        "|wave_rate=%u|wvr_rate=%u"
        "|wvr_t1_t2_q7=%u..%u|wvr_t2_ideal_q7=%u..%u"
        "|period_max_ps=%lu|direction_delta_max_ns=%lu"
        "|slip_rate=%u|finalist_samples=%u"
        "|stage_retries=%u|profile_fallbacks=%u|busy_polls=%u\r\n",
        (unsigned)s_autocal.config.min_valid_rate_per_mille,
        (unsigned)s_autocal.config.min_tuning_physical_rate_per_mille,
        (unsigned)s_autocal.config.min_physical_rate_per_mille,
        (unsigned)s_autocal.config.min_wave_valid_rate_per_mille,
        (unsigned)s_autocal.config.min_wvr_good_rate_per_mille,
        (unsigned)s_autocal.config.wvr_t1_t2_min_q7,
        (unsigned)s_autocal.config.wvr_ratio_max_q7,
        (unsigned)s_autocal.config.wvr_t2_ideal_min_q7,
        (unsigned)s_autocal.config.wvr_ratio_max_q7,
        (unsigned long)s_autocal.config.max_period_error_ps,
        (unsigned long)(s_autocal.config.max_direction_delta_ps /
                        INT64_C(1000)),
        (unsigned)s_autocal.config.max_cycle_slip_rate_per_mille,
        (unsigned)s_autocal.config.finalist_samples,
        (unsigned)s_autocal.config.max_stage_retries,
        (unsigned)s_autocal.config.max_profile_fallbacks,
        (unsigned)s_autocal.config.max_busy_polls);

    return MAX35103_AUTOCAL_RUNNING;
}

/* ---------------------------------------------------------------------------
 * Poll — advance FSM, log diagnostics, handle completion / failure
 * ------------------------------------------------------------------------- */

Max35103AutoCalStatus MAX35103_Calibrate_Poll(void)
{
    Max35103AutoCalProgress progress;
    Max35103AutoCalStatus status;

    if (!s_active) {
        return MAX35103_AUTOCAL_OK;
    }

    status = MAX35103_AutoCalStep(&s_autocal);
    MAX35103_AutoCalGetProgress(&s_autocal, &progress);

    /* ── Diagnostics every CAL_DIAG_CANDIDATE_INTERVAL candidates ──── */
    const bool new_candidate_evaluated =
        progress.evaluated_candidate_count != 0U &&
        progress.evaluated_candidate_count != s_last_diagnostic_candidate;
    const bool discovery_diagnostic_due =
        (progress.evaluated_candidate_count %
         CAL_DIAG_CANDIDATE_INTERVAL) == 0U;

    if (new_candidate_evaluated &&
        (progress.state != MAX35103_AUTOCAL_STATE_DISCOVERY ||
         discovery_diagnostic_due)) {

        const Max35103AutoCalMetrics *metrics =
            &s_autocal.candidate_metrics;
        const Max35103Profile *candidate =
            &s_autocal.candidate_profile;
        const uint8_t dpl = (uint8_t)(
            (candidate->tof1 & MAX35103_TOF1_DPL_MASK) >> 4);
        const uint8_t pulse_count = (uint8_t)(candidate->tof1 >> 8);
        const uint8_t stop_polarity =
            (candidate->tof1 & MAX35103_TOF1_STOP_POL_MASK) != 0U
            ? 1U : 0U;
        const uint8_t charge_time =
            (uint8_t)(candidate->tof1 & MAX35103_TOF1_CT_MASK);

        /* Use 32-bit ns — embedded printf may not support %lld. */
        const int32_t tof_up_ns =
            (int32_t)(metrics->median_tof_up_ps / INT64_C(1000));
        const int32_t tof_down_ns =
            (int32_t)(metrics->median_tof_down_ps / INT64_C(1000));
        const int32_t direction_delta_ns =
            (int32_t)(metrics->direction_delta_ps / INT64_C(1000));
        const uint32_t period_error_ps =
            metrics->median_period_error_ps < 0
                ? 0U
                : metrics->median_period_error_ps > (int64_t)UINT32_MAX
                      ? UINT32_MAX
                      : (uint32_t)metrics->median_period_error_ps;

        cal_log(
            "AUTOCAL|DIAG|candidate=%lu"
            "|valid=%u/%u|physical=%u|wave=%u"
            "|valid_rate=%u|physical_rate=%u"
            "|wave_rate=%u|wvr_rate=%u"
            "|tof_up_ns=%ld|tof_dn_ns=%ld"
            "|direction_delta_ns=%ld"
            "|period_err_ns=%lu|slips=%u|slip_rate=%u"
            "|dpl=%u|pulses=%u|stop_pol=%u|ct=%u"
            "|gate_comm=%u|gate_dir=%u|gate_phys=%u"
            "|gate_period=%u|gate_wave=%u"
            "|gate_stage_wave=%u|gate_stat=%u\r\n",
            (unsigned long)progress.evaluated_candidate_count,
            (unsigned)metrics->valid_count,
            (unsigned)metrics->attempted_count,
            (unsigned)metrics->physical_count,
            (unsigned)metrics->wave_valid_count,
            (unsigned)metrics->valid_rate_per_mille,
            (unsigned)metrics->physical_rate_per_mille,
            (unsigned)metrics->wave_valid_rate_per_mille,
            (unsigned)metrics->wvr_good_rate_per_mille,
            (long)tof_up_ns,
            (long)tof_down_ns,
            (long)direction_delta_ns,
            (unsigned long)period_error_ps,
            (unsigned)metrics->cycle_slip_count,
            (unsigned)metrics->cycle_slip_rate_per_mille,
            (unsigned)dpl,
            (unsigned)pulse_count,
            (unsigned)stop_polarity,
            (unsigned)charge_time,
            metrics->communication_gate ? 1U : 0U,
            metrics->direction_gate ? 1U : 0U,
            metrics->physical_gate ? 1U : 0U,
            metrics->period_gate ? 1U : 0U,
            metrics->waveform_gate ? 1U : 0U,
            metrics->stage_waveform_gate ? 1U : 0U,
            metrics->statistics_gate ? 1U : 0U);

        s_last_diagnostic_candidate =
            progress.evaluated_candidate_count;
    }

    /* ── Fallback (backtrack) log ───────────────────────────────────── */
    const bool fallback_changed =
        progress.profile_fallbacks_used != s_last_logged_fallback;
    if (fallback_changed) {
        cal_log(
            "AUTOCAL|BACKTRACK|from=%s|to=%s"
            "|fallback=%u/%u|finalist=%u/%u\r\n",
            MAX35103_AutoCalStateName(s_last_logged_state),
            MAX35103_AutoCalStateName(progress.state),
            (unsigned)progress.profile_fallbacks_used,
            (unsigned)s_autocal.config.max_profile_fallbacks,
            (unsigned)(progress.discovery_finalist_index + 1U),
            (unsigned)progress.discovery_finalist_count);
        s_last_logged_fallback =
            progress.profile_fallbacks_used;
    }

    /* ── Stage-pass log ─────────────────────────────────────────────── */
    const bool state_changed =
        progress.state != s_last_logged_state;
    if (state_changed && !fallback_changed &&
        s_last_logged_state != MAX35103_AUTOCAL_STATE_IDLE &&
        progress.state != MAX35103_AUTOCAL_STATE_FAILED &&
        progress.state != MAX35103_AUTOCAL_STATE_COMPLETE) {
        const Max35103Profile *selected =
            &s_autocal.selected_profile;
        cal_log(
            "AUTOCAL|STAGE_PASS|from=%s|to=%s"
            "|TOF1=%04X|DLY=%u\r\n",
            MAX35103_AutoCalStateName(s_last_logged_state),
            MAX35103_AutoCalStateName(progress.state),
            (unsigned)selected->tof1,
            (unsigned)selected->tof_measurement_delay);
    }

    /* ── Stage-retry log ────────────────────────────────────────────── */
    if (!state_changed &&
        progress.stage_retry_count != s_last_logged_retry) {
        cal_log(
            "AUTOCAL|STAGE_RETRY|state=%s|retry=%u/%u\r\n",
            MAX35103_AutoCalStateName(progress.state),
            (unsigned)progress.stage_retry_count,
            (unsigned)s_autocal.config.max_stage_retries);
        s_last_logged_retry = progress.stage_retry_count;
    }

    /* ── Periodic state log ─────────────────────────────────────────── */
    if (state_changed ||
        progress.evaluated_candidate_count >=
            s_last_logged_candidate + CAL_LOG_CANDIDATE_INTERVAL) {
        cal_log(
            "AUTOCAL|state=%s|candidate=%lu/%lu|sample=%u/%u"
            "|retry=%u|fallback=%u|evaluated=%lu"
            "|measurements=%lu|driver=%d\r\n",
            MAX35103_AutoCalStateName(progress.state),
            (unsigned long)progress.candidate_index,
            (unsigned long)progress.candidate_count,
            (unsigned)progress.sample_index,
            (unsigned)progress.sample_target,
            (unsigned)progress.stage_retry_count,
            (unsigned)progress.profile_fallbacks_used,
            (unsigned long)progress.evaluated_candidate_count,
            (unsigned long)progress.attempted_measurement_count,
            (int)progress.last_driver_status);

        s_last_logged_state     = progress.state;
        s_last_logged_candidate = progress.evaluated_candidate_count;
        s_last_logged_retry     = progress.stage_retry_count;
    }

    /* ── Completion / failure ───────────────────────────────────────── */
    if (status == MAX35103_AUTOCAL_COMPLETE) {
        const Max35103AutoCalStatus report_status =
            MAX35103_AutoCalGetReport(&s_autocal, &s_autocal_report);

        if (report_status == MAX35103_AUTOCAL_COMPLETE) {
            const Max35103Profile *profile =
                &s_autocal_report.selected_profile;

            cal_log(
                "AUTOCAL|PASS|confidence=%u|valid=%u/1000"
                "|physical=%u/1000|wave=%u/1000"
                "|wvr_up=%u/1000|wvr_dn=%u/1000"
                "|wvr_both=%u/1000|perturb=%u/%u|reset=%u"
                "|fallbacks=%u|crc=%08lX\r\n",
                (unsigned)s_autocal_report.confidence,
                (unsigned)s_autocal_report.verification
                    .valid_rate_per_mille,
                (unsigned)s_autocal_report.verification
                    .physical_rate_per_mille,
                (unsigned)s_autocal_report.verification
                    .wave_valid_rate_per_mille,
                (unsigned)s_autocal_report.verification
                    .wvr_up_good_rate_per_mille,
                (unsigned)s_autocal_report.verification
                    .wvr_down_good_rate_per_mille,
                (unsigned)s_autocal_report.verification
                    .wvr_good_rate_per_mille,
                (unsigned)s_autocal_report.perturbation_passed,
                (unsigned)s_autocal_report.perturbation_tested,
                s_autocal_report.reset_verified ? 1U : 0U,
                (unsigned)s_autocal_report.profile_fallbacks_used,
                (unsigned long)s_autocal_report.evidence_crc32);

            cal_log(
                "AUTOCAL|PROFILE|TOF1=%04X|TOF2=%04X"
                "|TOF3=%04X|TOF4=%04X|TOF5=%04X"
                "|TOF6=%04X|TOF7=%04X|DLY=%04X\r\n",
                (unsigned)profile->tof1,
                (unsigned)profile->tof2,
                (unsigned)profile->tof3,
                (unsigned)profile->tof4,
                (unsigned)profile->tof5,
                (unsigned)profile->tof6,
                (unsigned)profile->tof7,
                (unsigned)profile->tof_measurement_delay);
        } else {
            cal_log("AUTOCAL|FAIL|report_status=%d\r\n",
                    (int)report_status);
        }

        s_active = false;
    } else if (status < MAX35103_AUTOCAL_OK) {
        const Max35103AutoCalMetrics *failure =
            &s_autocal.failure_metrics;
        const Max35103Profile *failure_profile =
            &s_autocal.failure_profile;
        const uint32_t failure_period_ps =
            failure->median_period_error_ps < 0
                ? 0U
                : failure->median_period_error_ps > (int64_t)UINT32_MAX
                      ? UINT32_MAX
                      : (uint32_t)failure->median_period_error_ps;

        cal_log(
            "AUTOCAL|FAIL|status=%d|state=%s"
            "|candidate=%lu|retry=%u|driver=%d"
            "|TOF1=%04X|DLY=%u\r\n",
            (int)status,
            MAX35103_AutoCalStateName(s_autocal.failure_state),
            (unsigned long)s_autocal.failure_candidate_index,
            (unsigned)s_autocal.failure_retry_count,
            (int)progress.last_driver_status,
            (unsigned)failure_profile->tof1,
            (unsigned)failure_profile->tof_measurement_delay);

        cal_log(
            "AUTOCAL|FAIL_METRICS"
            "|valid=%u/%u|physical=%u|wave=%u"
            "|valid_rate=%u|physical_rate=%u/%u|wave_rate=%u"
            "|period_error_ps=%lu|slips=%u"
            "|direction_delta_ns=%ld"
            "|gate_comm=%u|gate_dir=%u|gate_phys=%u|gate_period=%u"
            "|gate_stage_wave=%u|gate_stat=%u\r\n",
            (unsigned)failure->valid_count,
            (unsigned)failure->attempted_count,
            (unsigned)failure->physical_count,
            (unsigned)failure->wave_valid_count,
            (unsigned)failure->valid_rate_per_mille,
            (unsigned)failure->physical_rate_per_mille,
            (unsigned)failure->physical_rate_required_per_mille,
            (unsigned)failure->wave_valid_rate_per_mille,
            (unsigned long)failure_period_ps,
            (unsigned)failure->cycle_slip_count,
            (long)(failure->direction_delta_ps / INT64_C(1000)),
            failure->communication_gate ? 1U : 0U,
            failure->direction_gate ? 1U : 0U,
            failure->physical_gate ? 1U : 0U,
            failure->period_gate ? 1U : 0U,
            failure->stage_waveform_gate ? 1U : 0U,
            failure->statistics_gate ? 1U : 0U);

        s_active = false;
    }

    return status;
}

bool MAX35103_Calibrate_IsActive(void)
{
    return s_active;
}
