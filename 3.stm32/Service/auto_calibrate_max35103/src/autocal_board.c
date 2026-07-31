/**
 ******************************************************************************
 * @file    autocal_board.c
 * @brief   STM32 application adapter and diagnostics for MAX35103 auto-calibration
 ******************************************************************************
 *
 * @details
 * This file owns one board-level calibration session.  It supplies concrete
 * STM32 SPI/GPIO/UART objects, chooses product-specific acoustic parameters and
 * search limits, drives the portable cooperative state machine, and translates
 * internal progress into stable pipe-delimited UART records.
 *
 * Responsibilities intentionally remain separated:
 *
 * - max35103_autocal.c decides which profile to try, how measurements are
 *   scored, which gates pass, and when to retry or backtrack;
 * - max35103.c owns device protocol, result acquisition, and profile readback;
 * - max35103_stm32_hal.c owns STM32 HAL transport details;
 * - this file owns board wiring, singleton lifetime, tuning policy overrides,
 *   diagnostics, and fatal application startup behavior.
 *
 * AUTOCAL_Start() is a one-time/session setup action.  AUTOCAL_Poll() must be
 * called repeatedly from foreground context until it returns a terminal value.
 * The adapter does not start an RTOS task or interrupt and performs no dynamic
 * allocation.
 *
 * @par Diagnostic format
 * UART output uses records beginning with @c AUTOCAL|.  Raw register fields
 * are printed in hexadecimal where appropriate; time metrics are explicitly
 * suffixed with @c _ps or @c _ns; rates are printed on a 0..1000 per-mille
 * scale.  The log is observational and never controls search decisions.
 *
 * @warning HAL_UART_Transmit() uses HAL_MAX_DELAY.  Although each algorithm
 *          step is bounded, diagnostic emission itself may block until UART
 *          transmission completes.
 * @warning The file-static session is not reentrant.  Call Start(), Poll(), and
 *          GetReport() from one serialized foreground/task context.
 ******************************************************************************
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "autocal_board.h"
#include "max35103_autocal.h"
#include "max35103_stm32_hal.h"

/*
 * STM32 and driver objects are owned by the application composition root in
 * main.c.  This adapter borrows them; it neither allocates nor destroys them.
 * The symbol names intentionally encode the current board wiring.
 */
extern UART_HandleTypeDef huart2;
extern SPI_HandleTypeDef hspi1;
extern Max35103Stm32HalContext g_max35103_hal_context;
extern Max35103Transport g_max35103_transport;

/** Maximum number of measurement attempts in the board-owned sample workspace. */
#define MAX35103_AUTOCAL_SAMPLE_CAPACITY         128U
/** Stack buffer used to format one complete UART diagnostic record. */
#define MAX35103_AUTOCAL_LOG_BUFFER_SIZE         512U
/** DISCOVERY progress-log interval, expressed in evaluated candidates. */
#define MAX35103_AUTOCAL_LOG_CANDIDATE_INTERVAL  50U
/** Detailed candidate-metrics interval used during the large DISCOVERY grid. */
#define MAX35103_AUTOCAL_DIAG_CANDIDATE_INTERVAL 10U
/** Board's one-way acoustic path length, in micrometres (15 mm). */
#define MAX35103_ACOUSTIC_PATH_UM                15000U
/** Nominal installed ultrasonic transducer frequency, in hertz (1 MHz). */
#define MAX35103_TRANSDUCER_FREQUENCY_HZ         1000000U

/*
 * Singleton session state.
 *
 * s_driver is borrowed from AUTOCAL_Start().  Backend, calibrator, sample
 * workspace, and report are owned by this module for the process lifetime.
 */
static Max35103Driver *s_driver;
static Max35103AutoCalBackend s_backend;
static Max35103AutoCalibrator s_calibrator;
static Max35103AutoCalSample s_samples[MAX35103_AUTOCAL_SAMPLE_CAPACITY];
static Max35103AutoCalReport s_report;

/* Lifecycle flags and retained public status. */
static bool s_active;
static bool s_report_available;
static Max35103AutoCalStatus s_status = MAX35103_AUTOCAL_OK;

/*
 * Log de-duplication cursors.  They do not affect candidate selection or
 * state-machine behavior; they only suppress repeated UART records.
 */
static Max35103AutoCalState s_last_logged_state = MAX35103_AUTOCAL_STATE_IDLE;
static uint32_t s_last_logged_candidate;
static uint32_t s_last_diagnostic_candidate;
static uint8_t s_last_logged_retry;
static uint8_t s_last_logged_fallback;

/**
 * @brief Format and transmit one board diagnostic record over UART2.
 *
 * @param[in] format printf-compatible format string.
 * @param[in] ... Arguments referenced by @p format.
 *
 * vsnprintf() bounds the write to the local buffer.  When formatted output is
 * longer than the buffer, the transmitted record is truncated to the final
 * stored NUL boundary.  UART errors are intentionally ignored because logging
 * is diagnostic evidence and must not change calibration decisions.
 *
 * @warning Runs synchronously and may block in HAL_UART_Transmit().
 */
static void autocal_log(const char *format, ...)
{
    char buffer[MAX35103_AUTOCAL_LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length <= 0)
    {
        return;
    }
    const uint16_t txlen = (uint16_t)((length < (int)sizeof(buffer)) ? length : ((int)sizeof(buffer) - 1));
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)buffer, txlen, HAL_MAX_DELAY);
}

/**
 * @brief Initialize the board stack and enter the portable DISCOVERY stage.
 *
 * @param[in,out] driver Borrowed MAX35103 driver instance.
 * @param[in] seed_profile Complete known-safe seed profile copied by AutoCalInit.
 *
 * @warning Any mandatory initialization failure is logged and routed to the
 *          application's Error_Handler().
 */
void AUTOCAL_Start(Max35103Driver *driver, const Max35103Profile *seed_profile)
{
    Max35103Status driver_status;
    Max35103AutoCalStatus status;
    Max35103AutoCalConfig config;

    if ((driver == NULL) || (seed_profile == NULL))
    {
        autocal_log("AUTOCAL|START_FAIL|stage=validate|status=%d\r\n",
                    (int)MAX35103_AUTOCAL_INVALID_ARG);
        Error_Handler();
        return;
    }

    /*
     * Invalidate the previous session before touching hardware.  A caller can
     * never retrieve an old report after attempting to start a new search.
     */
    s_driver = driver;
    s_active = false;
    s_report_available = false;
    s_status = MAX35103_AUTOCAL_OK;
    memset(&s_report, 0, sizeof(s_report));

    /*
     * Rebuild the concrete transport from the board pin map.  The portable
     * algorithm receives this hardware only indirectly through the driver
     * backend bound later in this function.
     */
    driver_status = MAX35103_Stm32HalInitTransport(&g_max35103_hal_context,
                                                   &hspi1,
                                                   MAX_NSS_GPIO_Port,
                                                   MAX_NSS_Pin,
                                                   MAX_RST_GPIO_Port,
                                                   MAX_RST_Pin,
                                                   &g_max35103_transport);
    if (driver_status != MAX35103_OK)
    {
        autocal_log("AUTOCAL|START_FAIL|stage=transport|driver_status=%d\r\n", (int)driver_status);
        Error_Handler();
        return;
    }

    driver_status = MAX35103_Init(driver, &g_max35103_transport);
    if (driver_status != MAX35103_OK)
    {
        autocal_log("AUTOCAL|START_FAIL|stage=driver_init|driver_status=%d\r\n",
                    (int)driver_status);
        Error_Handler();
        return;
    }

    /*
     * Start from known device and driver state.  Calibration candidates are
     * volatile; reset does not authorize a configuration-flash write.
     */
    driver_status = MAX35103_ResetDevice(driver);
    if (driver_status != MAX35103_OK)
    {
        autocal_log("AUTOCAL|START_FAIL|stage=device_reset|driver_status=%d\r\n",
                    (int)driver_status);
        Error_Handler();
        return;
    }

    status = MAX35103_AutoCalDefaultConfig(
        &config, MAX35103_ACOUSTIC_PATH_UM, MAX35103_TRANSDUCER_FREQUENCY_HZ);
    if (status != MAX35103_AUTOCAL_OK)
    {
        autocal_log("AUTOCAL|START_FAIL|stage=default_config|status=%d\r\n", (int)status);
        Error_Handler();
        return;
    }

    /*
     * Board-characterized policy overrides for the installed 15 mm path.
     *
     * DPL is fixed to the observed 1 MHz launch family.  DLY 0x001C..0x0023
     * corresponds to 7.00..8.75 us because one DLY tick is 250 ns.  Unit steps
     * deliberately evaluate every tick.  zero_flow_confirmed is an external
     * test condition asserted by this board application, not inferred by the
     * search engine.
     */
    config.dpl_min = 1U;
    config.dpl_max = 1U;
    config.ct_mask = 0x0FU;
    config.dly_min = 0x001CU;
    config.dly_max = 0x0023U;
    config.dly_coarse_step = 1U;
    config.dly_fine_step = 1U;
    config.zero_flow_confirmed = true;

    /* Bind the initialized core driver to the portable callback contract. */
    status = MAX35103_AutoCalBindDriver(driver, &s_backend);
    if (status != MAX35103_AUTOCAL_OK)
    {
        autocal_log("AUTOCAL|START_FAIL|stage=bind_driver|status=%d\r\n", (int)status);
        Error_Handler();
        return;
    }

    /*
     * The calibrator borrows s_samples.  Capacity must cover the largest
     * configured batch (the 128-sample verification stages).
     */
    status = MAX35103_AutoCalInit(&s_calibrator,
                                  &s_backend,
                                  &config,
                                  seed_profile,
                                  s_samples,
                                  MAX35103_AUTOCAL_SAMPLE_CAPACITY);
    if (status != MAX35103_AUTOCAL_OK)
    {
        autocal_log("AUTOCAL|START_FAIL|stage=autocal_init|status=%d\r\n", (int)status);
        Error_Handler();
        return;
    }

    s_last_logged_state = MAX35103_AUTOCAL_STATE_IDLE;
    s_last_logged_candidate = 0U;
    s_last_diagnostic_candidate = 0U;
    s_last_logged_retry = 0U;
    s_last_logged_fallback = 0U;

    /* Clear previous evidence and enter DISCOVERY. */
    status = MAX35103_AutoCalStart(&s_calibrator);
    if (status != MAX35103_AUTOCAL_RUNNING)
    {
        autocal_log("AUTOCAL|START_FAIL|stage=autocal_start|status=%d\r\n", (int)status);
        Error_Handler();
        return;
    }
    s_active = true;
    s_status = MAX35103_AUTOCAL_RUNNING;

    /* LOG: START param dump */
    autocal_log("AUTOCAL|START|path_um=%lu|arrival_ns=%ld..%ld"
                "|dly_ticks=%u..%u|dpl=%u..%u|ct_mask=%02X"
                "|discovery_candidates=%lu|zero_flow=%u\r\n",
                (unsigned long)s_calibrator.config.acoustic_path_length_um,
                (long)(s_calibrator.config.expected_min_tof_ps / INT64_C(1000)),
                (long)(s_calibrator.config.expected_max_tof_ps / INT64_C(1000)),
                (unsigned)s_calibrator.config.dly_min,
                (unsigned)s_calibrator.config.dly_max,
                (unsigned)s_calibrator.config.dpl_min,
                (unsigned)s_calibrator.config.dpl_max,
                (unsigned)s_calibrator.config.ct_mask,
                (unsigned long)s_calibrator.candidate_count,
                s_calibrator.config.zero_flow_confirmed ? 1U : 0U);

    /* LOG: SEED profile dump */
    autocal_log("AUTOCAL|SEED|TOF1=%04X|TOF2=%04X|TOF3=%04X"
                "|TOF4=%04X|TOF5=%04X|TOF6=%04X|TOF7=%04X|DLY=%04X\r\n",
                (unsigned)seed_profile->tof1,
                (unsigned)seed_profile->tof2,
                (unsigned)seed_profile->tof3,
                (unsigned)seed_profile->tof4,
                (unsigned)seed_profile->tof5,
                (unsigned)seed_profile->tof6,
                (unsigned)seed_profile->tof7,
                (unsigned)seed_profile->tof_measurement_delay);

    /* LOG: POLICY validation gates */
    autocal_log("AUTOCAL|POLICY|valid_rate=%u"
                "|physical_tuning=%u|physical_verify=%u"
                "|wave_rate=%u|wvr_rate=%u"
                "|wvr_t1_t2_q7=%u..%u|wvr_t2_ideal_q7=%u..%u"
                "|period_max_ps=%lu|direction_delta_max_ns=%lu"
                "|slip_rate=%u|finalist_samples=%u"
                "|stage_retries=%u|profile_fallbacks=%u|busy_polls=%u\r\n",
                (unsigned)s_calibrator.config.min_valid_rate_per_mille,
                (unsigned)s_calibrator.config.min_tuning_physical_rate_per_mille,
                (unsigned)s_calibrator.config.min_physical_rate_per_mille,
                (unsigned)s_calibrator.config.min_wave_valid_rate_per_mille,
                (unsigned)s_calibrator.config.min_wvr_good_rate_per_mille,
                (unsigned)s_calibrator.config.wvr_t1_t2_min_q7,
                (unsigned)s_calibrator.config.wvr_ratio_max_q7,
                (unsigned)s_calibrator.config.wvr_t2_ideal_min_q7,
                (unsigned)s_calibrator.config.wvr_ratio_max_q7,
                (unsigned long)s_calibrator.config.max_period_error_ps,
                (unsigned long)(s_calibrator.config.max_direction_delta_ps / INT64_C(1000)),
                (unsigned)s_calibrator.config.max_cycle_slip_rate_per_mille,
                (unsigned)s_calibrator.config.finalist_samples,
                (unsigned)s_calibrator.config.max_stage_retries,
                (unsigned)s_calibrator.config.max_profile_fallbacks,
                (unsigned)s_calibrator.config.max_busy_polls);
}

/**
 * @brief Advance the singleton calibration session and emit changed diagnostics.
 *
 * @return The retained Max35103AutoCalStatus for the active or latest session.
 *
 * Exactly one portable state-machine step is executed while @c s_active is
 * true.  Candidate metrics are inspected only after the step returns, so UART
 * diagnostics describe stable values produced by the algorithm.
 */
Max35103AutoCalStatus AUTOCAL_Poll(void)
{
    Max35103AutoCalProgress progress;
    Max35103AutoCalStatus status;

    if (!s_active)
    {
        return s_status;
    }

    /* One bounded algorithm action: reset, configure, measure, or transition. */
    status = MAX35103_AutoCalStep(&s_calibrator);
    s_status = status;
    MAX35103_AutoCalGetProgress(&s_calibrator, &progress);

    /*
     * Per-candidate diagnostics.
     *
     * Discovery may contain a large Cartesian product, so detailed output is
     * decimated there.  Later stages log every newly evaluated candidate
     * because each candidate changes only one tuning dimension and is useful
     * for engineering review.
     */
    const bool new_evaluated = progress.evaluated_candidate_count != 0U &&
                               progress.evaluated_candidate_count != s_last_diagnostic_candidate;
    const bool diag_due =
        (progress.evaluated_candidate_count % MAX35103_AUTOCAL_DIAG_CANDIDATE_INTERVAL) == 0U;
    if (new_evaluated && (progress.state != MAX35103_AUTOCAL_STATE_DISCOVERY || diag_due))
    {
        const Max35103AutoCalMetrics *m = &s_calibrator.candidate_metrics;
        const Max35103Profile *c = &s_calibrator.candidate_profile;
        /* Decode TOF1 fields only for human-readable diagnostics. */
        const uint8_t dpl = (uint8_t)((c->tof1 & MAX35103_TOF1_DPL_MASK) >> 4);
        const uint8_t pl = (uint8_t)(c->tof1 >> 8);
        const uint8_t pol = (c->tof1 & MAX35103_TOF1_STOP_POL_MASK) != 0U ? 1U : 0U;
        const uint8_t ct = (uint8_t)(c->tof1 & MAX35103_TOF1_CT_MASK);
        const int32_t up_ns = (int32_t)(m->median_tof_up_ps / INT64_C(1000));
        const int32_t dn_ns = (int32_t)(m->median_tof_down_ps / INT64_C(1000));
        const int32_t dd_ns = (int32_t)(m->direction_delta_ps / INT64_C(1000));
        /*
         * The UART format uses unsigned long for broad embedded-libc
         * compatibility.  Saturate the non-negative period metric before the
         * narrowing conversion instead of allowing implementation-defined
         * signed/width behavior in printf.
         */
        const uint32_t pe_ps = m->median_period_error_ps < 0 ? 0U
                               : m->median_period_error_ps > (int64_t)UINT32_MAX
                                   ? UINT32_MAX
                                   : (uint32_t)m->median_period_error_ps;

        autocal_log("AUTOCAL|DIAG_CFG|state=%s|candidate=%lu"
                    "|stage_candidate=%lu/%lu|retry=%u"
                    "|DPL=%u|PL=%u|CT=%u|POL=%u|DLY=%u"
                    "|WVR_UP=%u,%u|WVR_DN=%u,%u\r\n",
                    MAX35103_AutoCalStateName(progress.state),
                    (unsigned long)progress.evaluated_candidate_count,
                    (unsigned long)progress.candidate_index,
                    (unsigned long)progress.candidate_count,
                    (unsigned)progress.stage_retry_count,
                    (unsigned)dpl,
                    (unsigned)pl,
                    (unsigned)ct,
                    (unsigned)pol,
                    (unsigned)c->tof_measurement_delay,
                    (unsigned)s_calibrator.last_wvr_up_t1_t2_q7,
                    (unsigned)s_calibrator.last_wvr_up_t2_ideal_q7,
                    (unsigned)s_calibrator.last_wvr_down_t1_t2_q7,
                    (unsigned)s_calibrator.last_wvr_down_t2_ideal_q7);

        autocal_log("AUTOCAL|DIAG|candidate=%lu"
                    "|valid=%u/%u|physical=%u|wave=%u"
                    "|valid_rate=%u|physical_rate=%u/%u|wave_rate=%u"
                    "|wvr_up=%u|wvr_dn=%u|wvr_both=%u"
                    "|arrival_up_ns=%ld|arrival_down_ns=%ld"
                    "|direction_delta_ns=%ld"
                    "|period_error_ps=%lu|slips=%u|slip_rate=%u"
                    "|gate_comm=%u|gate_dir=%u|gate_phys=%u|gate_period=%u"
                    "|gate_wave=%u|gate_stage_wave=%u|gate_stat=%u\r\n",
                    (unsigned long)progress.evaluated_candidate_count,
                    (unsigned)m->valid_count,
                    (unsigned)m->attempted_count,
                    (unsigned)m->physical_count,
                    (unsigned)m->wave_valid_count,
                    (unsigned)m->valid_rate_per_mille,
                    (unsigned)m->physical_rate_per_mille,
                    (unsigned)m->physical_rate_required_per_mille,
                    (unsigned)m->wave_valid_rate_per_mille,
                    (unsigned)m->wvr_up_good_rate_per_mille,
                    (unsigned)m->wvr_down_good_rate_per_mille,
                    (unsigned)m->wvr_good_rate_per_mille,
                    (long)up_ns,
                    (long)dn_ns,
                    (long)dd_ns,
                    (unsigned long)pe_ps,
                    (unsigned)m->cycle_slip_count,
                    (unsigned)m->cycle_slip_rate_per_mille,
                    m->communication_gate ? 1U : 0U,
                    m->direction_gate ? 1U : 0U,
                    m->physical_gate ? 1U : 0U,
                    m->period_gate ? 1U : 0U,
                    m->waveform_gate ? 1U : 0U,
                    m->stage_waveform_gate ? 1U : 0U,
                    m->statistics_gate ? 1U : 0U);

        s_last_diagnostic_candidate = progress.evaluated_candidate_count;
    }

    /*
     * A fallback means a later stage exhausted its local retry policy and the
     * algorithm restarted from the next distinct DISCOVERY launch finalist.
     */
    const bool fb_changed = progress.profile_fallbacks_used != s_last_logged_fallback;
    if (fb_changed)
    {
        autocal_log("AUTOCAL|BACKTRACK|from=%s|to=%s|fallback=%u/%u|finalist=%u/%u\r\n",
                    MAX35103_AutoCalStateName(s_last_logged_state),
                    MAX35103_AutoCalStateName(progress.state),
                    (unsigned)progress.profile_fallbacks_used,
                    (unsigned)s_calibrator.config.max_profile_fallbacks,
                    (unsigned)(progress.discovery_finalist_index + 1U),
                    (unsigned)progress.discovery_finalist_count);
        s_last_logged_fallback = progress.profile_fallbacks_used;
    }

    /*
     * A normal state change commits the previous stage's best profile.  Do not
     * label fallback or terminal transitions as ordinary stage passes.
     */
    const bool st_changed = progress.state != s_last_logged_state;
    if (st_changed && !fb_changed && s_last_logged_state != MAX35103_AUTOCAL_STATE_IDLE &&
        progress.state != MAX35103_AUTOCAL_STATE_FAILED &&
        progress.state != MAX35103_AUTOCAL_STATE_COMPLETE)
    {
        const Max35103Profile *sel = &s_calibrator.selected_profile;
        autocal_log("AUTOCAL|STAGE_PASS|from=%s|to=%s|TOF1=%04X|DLY=%u\r\n",
                    MAX35103_AutoCalStateName(s_last_logged_state),
                    MAX35103_AutoCalStateName(progress.state),
                    (unsigned)sel->tof1,
                    (unsigned)sel->tof_measurement_delay);
    }

    /* A retry restarts the same stage and keeps its discovery root profile. */
    if (!st_changed && progress.stage_retry_count != s_last_logged_retry)
    {
        autocal_log("AUTOCAL|STAGE_RETRY|state=%s|retry=%u/%u\r\n",
                    MAX35103_AutoCalStateName(progress.state),
                    (unsigned)progress.stage_retry_count,
                    (unsigned)s_calibrator.config.max_stage_retries);
        s_last_logged_retry = progress.stage_retry_count;
    }

    /*
     * Compact progress heartbeat: always emit on state changes, otherwise
     * decimate by evaluated-candidate count to keep UART volume bounded.
     */
    if (st_changed || progress.evaluated_candidate_count >=
                          s_last_logged_candidate + MAX35103_AUTOCAL_LOG_CANDIDATE_INTERVAL)
    {
        autocal_log("AUTOCAL|state=%s|candidate=%lu/%lu|sample=%u/%u"
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

        s_last_logged_state = progress.state;
        s_last_logged_candidate = progress.evaluated_candidate_count;
        s_last_logged_retry = progress.stage_retry_count;
    }

    /*
     * Terminal success handling.
     *
     * The portable service has already reset, reapplied, verified, and
     * checksummed the selected profile.  The board adapter obtains a value
     * copy of that report and applies the selected profile once more so the
     * device exits calibration in the exact reported operating state.
     */
    if (status == MAX35103_AUTOCAL_COMPLETE)
    {
        if (MAX35103_AutoCalGetReport(&s_calibrator, &s_report) == MAX35103_AUTOCAL_COMPLETE)
        {
            const Max35103Profile *profile = &s_report.selected_profile;

            autocal_log("AUTOCAL|PASS|confidence=%u|valid=%u/1000"
                        "|physical=%u/1000|wave=%u/1000"
                        "|wvr_up=%u/1000|wvr_dn=%u/1000"
                        "|wvr_both=%u/1000|perturb=%u/%u|reset=%u"
                        "|fallbacks=%u|crc=%08lX\r\n",
                        (unsigned)s_report.confidence,
                        (unsigned)s_report.verification.valid_rate_per_mille,
                        (unsigned)s_report.verification.physical_rate_per_mille,
                        (unsigned)s_report.verification.wave_valid_rate_per_mille,
                        (unsigned)s_report.verification.wvr_up_good_rate_per_mille,
                        (unsigned)s_report.verification.wvr_down_good_rate_per_mille,
                        (unsigned)s_report.verification.wvr_good_rate_per_mille,
                        (unsigned)s_report.perturbation_passed,
                        (unsigned)s_report.perturbation_tested,
                        s_report.reset_verified ? 1U : 0U,
                        (unsigned)s_report.profile_fallbacks_used,
                        (unsigned long)s_report.evidence_crc32);

            autocal_log("AUTOCAL|PROFILE|TOF1=%04X|TOF2=%04X|TOF3=%04X"
                        "|TOF4=%04X|TOF5=%04X|TOF6=%04X|TOF7=%04X|DLY=%04X\r\n",
                        (unsigned)profile->tof1,
                        (unsigned)profile->tof2,
                        (unsigned)profile->tof3,
                        (unsigned)profile->tof4,
                        (unsigned)profile->tof5,
                        (unsigned)profile->tof6,
                        (unsigned)profile->tof7,
                        (unsigned)profile->tof_measurement_delay);

            /* Publish the report only if the final operating profile applies. */
            const Max35103Status config_status =
                MAX35103_Configure(s_driver, &s_report.selected_profile);
            if (config_status == MAX35103_OK)
            {
                s_report_available = true;
                s_status = MAX35103_AUTOCAL_COMPLETE;
                autocal_log("AUTOCAL|CONFIG_APPLIED\r\n");
            }
            else
            {
                s_report_available = false;
                s_status = MAX35103_AUTOCAL_DRIVER_ERROR;
                autocal_log("AUTOCAL|FAIL|stage=apply_profile|driver_status=%d\r\n",
                            (int)config_status);
            }
        }
        else
        {
            s_report_available = false;
            s_status = MAX35103_AUTOCAL_DRIVER_ERROR;
            autocal_log("AUTOCAL|FAIL|report_status=%d\r\n", (int)status);
        }

        s_active = false;
    }
    /*
     * Negative statuses are terminal.  Log the closest scored candidate and
     * its gate evidence retained by autocal_fail(), then stop polling work.
     */
    else if (status < MAX35103_AUTOCAL_OK)
    {
        const Max35103AutoCalMetrics *f = &s_calibrator.failure_metrics;
        const Max35103Profile *fp = &s_calibrator.failure_profile;
        const uint32_t fp_ps = f->median_period_error_ps < 0 ? 0U
                               : f->median_period_error_ps > (int64_t)UINT32_MAX
                                   ? UINT32_MAX
                                   : (uint32_t)f->median_period_error_ps;
        autocal_log("AUTOCAL|FAIL|status=%d|state=%s|candidate=%lu|retry=%u|driver=%d"
                    "|TOF1=%04X|DLY=%u\r\n",
                    (int)status,
                    MAX35103_AutoCalStateName(s_calibrator.failure_state),
                    (unsigned long)s_calibrator.failure_candidate_index,
                    (unsigned)s_calibrator.failure_retry_count,
                    (int)progress.last_driver_status,
                    (unsigned)fp->tof1,
                    (unsigned)fp->tof_measurement_delay);
        autocal_log("AUTOCAL|FAIL_METRICS"
                    "|valid=%u/%u|physical=%u|wave=%u"
                    "|valid_rate=%u|physical_rate=%u/%u|wave_rate=%u"
                    "|period_error_ps=%lu|slips=%u|direction_delta_ns=%ld"
                    "|gate_comm=%u|gate_dir=%u|gate_phys=%u|gate_period=%u"
                    "|gate_stage_wave=%u|gate_stat=%u\r\n",
                    (unsigned)f->valid_count,
                    (unsigned)f->attempted_count,
                    (unsigned)f->physical_count,
                    (unsigned)f->wave_valid_count,
                    (unsigned)f->valid_rate_per_mille,
                    (unsigned)f->physical_rate_per_mille,
                    (unsigned)f->physical_rate_required_per_mille,
                    (unsigned)f->wave_valid_rate_per_mille,
                    (unsigned long)fp_ps,
                    (unsigned)f->cycle_slip_count,
                    (long)(f->direction_delta_ps / INT64_C(1000)),
                    f->communication_gate ? 1U : 0U,
                    f->direction_gate ? 1U : 0U,
                    f->physical_gate ? 1U : 0U,
                    f->period_gate ? 1U : 0U,
                    f->stage_waveform_gate ? 1U : 0U,
                    f->statistics_gate ? 1U : 0U);
        s_report_available = false;
        s_status = status;
        s_active = false;
    }

    return s_status;
}

/**
 * @brief Copy the board-retained report after successful final application.
 *
 * @param[out] report Caller-owned destination.
 * @return true when a report was available and copied; otherwise false.
 */
bool AUTOCAL_GetReport(Max35103AutoCalReport *report)
{
    if (report == NULL || !s_report_available)
    {
        return false;
    }

    *report = s_report;
    return true;
}