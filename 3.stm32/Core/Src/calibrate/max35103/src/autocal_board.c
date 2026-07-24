/**
 * @file autocal_board.c
 * @brief STM32 board integration for the MAX35103 auto-calibration service.
 */

#include <stdarg.h>
#include <stdio.h>

#include "main.h"
#include "autocal_board.h"
#include "max35103_autocal.h"
#include "max35103_stm32_hal.h"

/* STM32 objects owned by the application composition root in main.c. */
extern UART_HandleTypeDef huart2;
extern SPI_HandleTypeDef hspi1;
extern Max35103Stm32HalContext g_max35103_hal_context;
extern Max35103Transport g_max35103_transport;

#define MAX35103_AUTOCAL_SAMPLE_CAPACITY         128U
#define MAX35103_AUTOCAL_LOG_BUFFER_SIZE         512U
#define MAX35103_AUTOCAL_LOG_CANDIDATE_INTERVAL  50U
#define MAX35103_AUTOCAL_DIAG_CANDIDATE_INTERVAL 10U
#define MAX35103_ACOUSTIC_PATH_UM                15000U
#define MAX35103_TRANSDUCER_FREQUENCY_HZ         1000000U

static Max35103Driver *s_driver;
static Max35103AutoCalBackend s_backend;
static Max35103AutoCalibrator s_calibrator;
static Max35103AutoCalSample s_samples[MAX35103_AUTOCAL_SAMPLE_CAPACITY];
static Max35103AutoCalReport s_report;

static Max35103AutoCalStatus s_terminal_status = MAX35103_AUTOCAL_RUNNING;

static bool s_active;
static Max35103AutoCalState s_last_logged_state = MAX35103_AUTOCAL_STATE_IDLE;
static uint32_t s_last_logged_candidate;
static uint32_t s_last_diagnostic_candidate;
static uint8_t s_last_logged_retry;
static uint8_t s_last_logged_fallback;

/**
 * @brief Format and transmit one auto-calibration diagnostic line over UART.
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
    const uint16_t txlen =
        (uint16_t)((length < (int)sizeof(buffer)) ? length : ((int)sizeof(buffer) - 1));
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)buffer, txlen, HAL_MAX_DELAY);
}

/**
 * @brief Initialize the board backend and start a new auto-calibration run.
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

    s_driver = driver;

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

    /* HIL tuning for 15 mm acoustic path */
    config.dpl_min = 1U;
    config.dpl_max = 1U;
    config.ct_mask = 0x0FU;
    config.dly_min = 0x001CU;
    config.dly_max = 0x0023U;
    config.dly_coarse_step = 1U;
    config.dly_fine_step = 1U;
    config.zero_flow_confirmed = true;

    status = MAX35103_AutoCalBindDriver(driver, &s_backend);
    if (status != MAX35103_AUTOCAL_OK)
    {
        autocal_log("AUTOCAL|START_FAIL|stage=bind_driver|status=%d\r\n", (int)status);
        Error_Handler();
        return;
    }

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

    status = MAX35103_AutoCalStart(&s_calibrator);
    if (status != MAX35103_AUTOCAL_RUNNING)
    {
        autocal_log("AUTOCAL|START_FAIL|stage=autocal_start|status=%d\r\n", (int)status);
        Error_Handler();
        return;
    }
    s_active = true;

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
 * @brief Advance auto-calibration and emit progress or terminal diagnostics.
 */
Max35103AutoCalStatus AUTOCAL_Poll(void)
{
    Max35103AutoCalProgress progress;
    Max35103AutoCalStatus status;

    if (!s_active)
    {
        return s_terminal_status;
    }

    status = MAX35103_AutoCalStep(&s_calibrator);
    MAX35103_AutoCalGetProgress(&s_calibrator, &progress);

    /* Per-candidate configuration diagnostics. */
    const bool new_evaluated = progress.evaluated_candidate_count != 0U &&
                               progress.evaluated_candidate_count != s_last_diagnostic_candidate;
    const bool diag_due =
        (progress.evaluated_candidate_count % MAX35103_AUTOCAL_DIAG_CANDIDATE_INTERVAL) == 0U;
    if (new_evaluated && (progress.state != MAX35103_AUTOCAL_STATE_DISCOVERY || diag_due))
    {
        const Max35103AutoCalMetrics *m = &s_calibrator.candidate_metrics;
        const Max35103Profile *c = &s_calibrator.candidate_profile;
        const uint8_t dpl = (uint8_t)((c->tof1 & MAX35103_TOF1_DPL_MASK) >> 4);
        const uint8_t pl = (uint8_t)(c->tof1 >> 8);
        const uint8_t pol = (c->tof1 & MAX35103_TOF1_STOP_POL_MASK) != 0U ? 1U : 0U;
        const uint8_t ct = (uint8_t)(c->tof1 & MAX35103_TOF1_CT_MASK);
        const int32_t up_ns = (int32_t)(m->median_tof_up_ps / INT64_C(1000));
        const int32_t dn_ns = (int32_t)(m->median_tof_down_ps / INT64_C(1000));
        const int32_t dd_ns = (int32_t)(m->direction_delta_ps / INT64_C(1000));
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

    /* BACKTRACK log */
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

    /* STAGE_PASS log */
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

    /* STAGE_RETRY log */
    if (!st_changed && progress.stage_retry_count != s_last_logged_retry)
    {
        autocal_log("AUTOCAL|STAGE_RETRY|state=%s|retry=%u/%u\r\n",
                    MAX35103_AutoCalStateName(progress.state),
                    (unsigned)progress.stage_retry_count,
                    (unsigned)s_calibrator.config.max_stage_retries);
        s_last_logged_retry = progress.stage_retry_count;
    }

    /* Periodic state log */
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

    /* Handle terminal states */
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
        }
        else
        {
            autocal_log("AUTOCAL|FAIL|report_status=%d\r\n", (int)status);
        }

        s_terminal_status = MAX35103_AUTOCAL_COMPLETE;
        s_active = false;
    }
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
        s_terminal_status = status;
        s_active = false;
    }

    return status;
}

bool AUTOCAL_GetSelectedProfile(Max35103Profile *profile)
{
    if (s_terminal_status != MAX35103_AUTOCAL_COMPLETE)
    {
        return false;
    }
    if (profile == NULL)
    {
        return false;
    }
    *profile = s_report.selected_profile;
    return true;
}

int64_t AUTOCAL_GetZeroFlowOffset(void)
{
    return s_report.zero_flow_offset_ps;
}
