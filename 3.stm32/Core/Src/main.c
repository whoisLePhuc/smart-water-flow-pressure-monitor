/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef FIRMWARE_BUILD_TESTS_FM24CL04B
#include "fram_test.h"
#endif

#if defined(FIRMWARE_BUILD_TESTS_MAX35103) && \
    defined(FIRMWARE_BUILD_MAX35103_AUTOCAL)
#error "MAX35103 HIL test and AutoCal cannot run in the same build"
#endif

#if defined(FIRMWARE_BUILD_TESTS_MAX35103) || \
    defined(FIRMWARE_BUILD_MAX35103_AUTOCAL)
#include "max35103.h"
#include "max35103_stm32_hal.h"
#endif

#ifdef FIRMWARE_BUILD_TESTS_MAX35103
#include "max35103_test.h"
#endif

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
#include "max35103_autocal.h"
#endif

#ifdef FIRMWARE_BUILD_TESTS_ZSSC3241
#include "zssc3241_test.h"
#endif
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
#if defined(FIRMWARE_BUILD_TESTS_MAX35103) || \
    defined(FIRMWARE_BUILD_MAX35103_AUTOCAL)
static const Max35103Profile g_max35103_profile = {
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
static Max35103Stm32HalContext g_max35103_hal_context;
static Max35103Transport g_max35103_transport;
#endif

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
#define MAX35103_AUTOCAL_SAMPLE_CAPACITY 128U
#define MAX35103_AUTOCAL_LOG_BUFFER_SIZE 512U
#define MAX35103_AUTOCAL_LOG_CANDIDATE_INTERVAL 50U
#define MAX35103_AUTOCAL_DIAG_CANDIDATE_INTERVAL 10U
#define MAX35103_ACOUSTIC_PATH_UM 15000U
#define MAX35103_TRANSDUCER_FREQUENCY_HZ 1000000U

static Max35103Driver g_max35103_driver;
static Max35103AutoCalBackend g_max35103_autocal_backend;
static Max35103AutoCalibrator g_max35103_autocal;
static Max35103AutoCalSample
    g_max35103_autocal_samples[MAX35103_AUTOCAL_SAMPLE_CAPACITY];
static Max35103AutoCalReport g_max35103_autocal_report;

static bool g_max35103_autocal_active;
static Max35103AutoCalState g_max35103_last_logged_state =
    MAX35103_AUTOCAL_STATE_IDLE;
static uint32_t g_max35103_last_logged_candidate;
static uint32_t g_max35103_last_diagnostic_candidate;
static uint8_t g_max35103_last_logged_retry;
static uint8_t g_max35103_last_logged_fallback;
#endif

#ifdef FIRMWARE_BUILD_TESTS_ZSSC3241
static const Zssc3241TestConfig g_zssc3241_test_config = {
    .hi2c = &hi2c1,
    .address_7bit = 0x28U,
    .reset_port = NULL,
    .reset_pin = 0U,
    .reset_available = false,
    .eoc_available = false,
    .run_full_nvm_dump = false,
    .run_cyclic_test = false,
    .cyclic_settle_ms = 0U,
    .pressure_mapping_enabled = false,
    .pressure_code_min = 0U,
    .pressure_code_max = 0U,
    .pressure_min_mbar = 0,
    .pressure_max_mbar = 0,
    .driver_config = NULL,
};
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_RTC_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
static void MAX35103_AutoCalLog(const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
static Max35103AutoCalStatus MAX35103_AutoCalBoardStart(void);
static void MAX35103_AutoCalPoll(void);
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint32_t ADC_Read_Voltage(void);

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
static void MAX35103_AutoCalLog(const char *format, ...)
{
    char buffer[MAX35103_AUTOCAL_LOG_BUFFER_SIZE];
    va_list args;

    va_start(args, format);
    const int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (length <= 0) {
        return;
    }

    const uint16_t transmit_length =
        (length < (int)sizeof(buffer))
            ? (uint16_t)length
            : (uint16_t)(sizeof(buffer) - 1U);

    (void)HAL_UART_Transmit(
        &huart2,
        (uint8_t *)buffer,
        transmit_length,
        HAL_MAX_DELAY);
}

static Max35103AutoCalStatus MAX35103_AutoCalBoardStart(void)
{
    Max35103Status driver_status;
    Max35103AutoCalStatus status;
    Max35103AutoCalConfig config;

    driver_status = MAX35103_Stm32HalInitTransport(
        &g_max35103_hal_context,
        &hspi1,
        MAX_NSS_GPIO_Port,
        MAX_NSS_Pin,
        MAX_RST_GPIO_Port,
        MAX_RST_Pin,
        &g_max35103_transport);
    if (driver_status != MAX35103_OK) {
        MAX35103_AutoCalLog(
            "AUTOCAL|START_FAIL|stage=transport|driver_status=%d\r\n",
            (int)driver_status);
        return MAX35103_AUTOCAL_DRIVER_ERROR;
    }

    driver_status = MAX35103_Init(
        &g_max35103_driver,
        &g_max35103_transport);
    if (driver_status != MAX35103_OK) {
        MAX35103_AutoCalLog(
            "AUTOCAL|START_FAIL|stage=driver_init|driver_status=%d\r\n",
            (int)driver_status);
        return MAX35103_AUTOCAL_DRIVER_ERROR;
    }

    driver_status = MAX35103_ResetDevice(&g_max35103_driver);
    if (driver_status != MAX35103_OK) {
        MAX35103_AutoCalLog(
            "AUTOCAL|START_FAIL|stage=device_reset|driver_status=%d\r\n",
            (int)driver_status);
        return MAX35103_AUTOCAL_DRIVER_ERROR;
    }

    status = MAX35103_AutoCalDefaultConfig(
        &config,
        MAX35103_ACOUSTIC_PATH_UM,
        MAX35103_TRANSDUCER_FREQUENCY_HZ);
    if (status != MAX35103_AUTOCAL_OK) {
        MAX35103_AutoCalLog(
            "AUTOCAL|START_FAIL|stage=default_config|status=%d\r\n",
            (int)status);
        return status;
    }

    /*
     * HIL tuning for the 15 mm acoustic path. The previous 4.5..6.5 us
     * discovery points locked onto early feed-through/ringing. Keep the
     * receiver disabled until 7 us, then sweep across the leading edge of the
     * expected 8.375..11.714 us direct-arrival window.
     */
    config.dpl_min = 1U;
    config.dpl_max = 1U;
    config.dly_min = 0x001CU; /* 7.00 us. */
    config.dly_max = 0x0023U; /* 8.75 us. */
    config.dly_coarse_step = 1U; /* 0.25 us; includes both endpoints. */
    config.dly_fine_step = 1U;

    /*
     * Keep false for normal tuning. Set true only while the pipe is full and
     * zero flow has been independently confirmed.
     */
    config.zero_flow_confirmed = false;

    status = MAX35103_AutoCalBindDriver(
        &g_max35103_driver,
        &g_max35103_autocal_backend);
    if (status != MAX35103_AUTOCAL_OK) {
        MAX35103_AutoCalLog(
            "AUTOCAL|START_FAIL|stage=bind_driver|status=%d\r\n",
            (int)status);
        return status;
    }

    status = MAX35103_AutoCalInit(
        &g_max35103_autocal,
        &g_max35103_autocal_backend,
        &config,
        &g_max35103_profile,
        g_max35103_autocal_samples,
        (uint16_t)(sizeof(g_max35103_autocal_samples) /
                   sizeof(g_max35103_autocal_samples[0])));
    if (status != MAX35103_AUTOCAL_OK) {
        MAX35103_AutoCalLog(
            "AUTOCAL|START_FAIL|stage=autocal_init|status=%d\r\n",
            (int)status);
        return status;
    }

    g_max35103_last_logged_state = MAX35103_AUTOCAL_STATE_IDLE;
    g_max35103_last_logged_candidate = 0U;
    g_max35103_last_diagnostic_candidate = 0U;
    g_max35103_last_logged_retry = 0U;
    g_max35103_last_logged_fallback = 0U;

    status = MAX35103_AutoCalStart(&g_max35103_autocal);
    if (status != MAX35103_AUTOCAL_RUNNING) {
        MAX35103_AutoCalLog(
            "AUTOCAL|START_FAIL|stage=autocal_start|status=%d\r\n",
            (int)status);
    }
    return status;
}

static void MAX35103_AutoCalPoll(void)
{
    Max35103AutoCalProgress progress;
    Max35103AutoCalStatus status;

    if (!g_max35103_autocal_active) {
        return;
    }

    status = MAX35103_AutoCalStep(&g_max35103_autocal);
    MAX35103_AutoCalGetProgress(
        &g_max35103_autocal,
        &progress);

    const bool new_candidate_evaluated =
        progress.evaluated_candidate_count != 0U &&
        progress.evaluated_candidate_count !=
            g_max35103_last_diagnostic_candidate;
    const bool discovery_diagnostic_due =
        (progress.evaluated_candidate_count %
            MAX35103_AUTOCAL_DIAG_CANDIDATE_INTERVAL) == 0U;
    if (new_candidate_evaluated &&
        (progress.state != MAX35103_AUTOCAL_STATE_DISCOVERY ||
         discovery_diagnostic_due)) {
        const Max35103AutoCalMetrics *metrics =
            &g_max35103_autocal.candidate_metrics;
        const Max35103Profile *candidate =
            &g_max35103_autocal.candidate_profile;
        const uint8_t dpl = (uint8_t)(
            (candidate->tof1 & MAX35103_TOF1_DPL_MASK) >> 4);
        const uint8_t pulse_count = (uint8_t)(candidate->tof1 >> 8);
        const uint8_t stop_polarity =
            (candidate->tof1 & MAX35103_TOF1_STOP_POL_MASK) != 0U
            ? 1U
            : 0U;
        const uint8_t charge_time =
            (uint8_t)(candidate->tof1 & MAX35103_TOF1_CT_MASK);

        /*
         * Use 32-bit nanoseconds in the UART diagnostic. Some embedded
         * printf/vsnprintf configurations do not support "%lld"; using it
         * shifts the following variadic arguments and corrupts gate_* logs.
         */
        const int32_t tof_up_ns =
            (int32_t)(metrics->median_tof_up_ps / INT64_C(1000));
        const int32_t tof_down_ns =
            (int32_t)(metrics->median_tof_down_ps / INT64_C(1000));
        const int32_t direction_delta_ns =
            (int32_t)(metrics->direction_delta_ps / INT64_C(1000));
        /*
         * Period errors in a valid candidate are far below UINT32_MAX ps.
         * Saturate corrupt/out-of-range diagnostics so embedded printf does
         * not need "%lld", which is unavailable in some newlib-nano builds.
         */
        const uint32_t period_error_ps =
            metrics->median_period_error_ps < 0
                ? 0U
                : metrics->median_period_error_ps >
                          (int64_t)UINT32_MAX
                      ? UINT32_MAX
                      : (uint32_t)metrics->median_period_error_ps;

        MAX35103_AutoCalLog(
            "AUTOCAL|DIAG_CFG|state=%s|candidate=%lu"
            "|stage_candidate=%lu/%lu|retry=%u"
            "|DPL=%u|PL=%u|CT=%u|POL=%u|DLY=%u"
            "|WVR_UP=%u,%u|WVR_DN=%u,%u\r\n",
            MAX35103_AutoCalStateName(progress.state),
            (unsigned long)progress.evaluated_candidate_count,
            (unsigned long)progress.candidate_index,
            (unsigned long)progress.candidate_count,
            (unsigned)progress.stage_retry_count,
            (unsigned)dpl,
            (unsigned)pulse_count,
            (unsigned)charge_time,
            (unsigned)stop_polarity,
            (unsigned)candidate->tof_measurement_delay,
            (unsigned)g_max35103_autocal.last_wvr_up_t1_t2_q7,
            (unsigned)g_max35103_autocal.last_wvr_up_t2_ideal_q7,
            (unsigned)g_max35103_autocal.last_wvr_down_t1_t2_q7,
            (unsigned)g_max35103_autocal.last_wvr_down_t2_ideal_q7);

        MAX35103_AutoCalLog(
            "AUTOCAL|DIAG|candidate=%lu"
            "|valid=%u/%u|physical=%u|wave=%u"
            "|valid_rate=%u|physical_rate=%u/%u|wave_rate=%u"
            "|wvr_up=%u|wvr_dn=%u|wvr_both=%u"
            "|arrival_up_ns=%ld|arrival_down_ns=%ld"
            "|direction_delta_ns=%ld"
            "|period_error_ps=%lu|slips=%u|slip_rate=%u"
            "|gate_comm=%u|gate_dir=%u|gate_phys=%u|gate_period=%u"
            "|gate_wave=%u|gate_stage_wave=%u|gate_stat=%u\r\n",
            (unsigned long)progress.evaluated_candidate_count,
            (unsigned)metrics->valid_count,
            (unsigned)metrics->attempted_count,
            (unsigned)metrics->physical_count,
            (unsigned)metrics->wave_valid_count,
            (unsigned)metrics->valid_rate_per_mille,
            (unsigned)metrics->physical_rate_per_mille,
            (unsigned)metrics->physical_rate_required_per_mille,
            (unsigned)metrics->wave_valid_rate_per_mille,
            (unsigned)metrics->wvr_up_good_rate_per_mille,
            (unsigned)metrics->wvr_down_good_rate_per_mille,
            (unsigned)metrics->wvr_good_rate_per_mille,
            (long)tof_up_ns,
            (long)tof_down_ns,
            (long)direction_delta_ns,
            (unsigned long)period_error_ps,
            (unsigned)metrics->cycle_slip_count,
            (unsigned)metrics->cycle_slip_rate_per_mille,
            metrics->communication_gate ? 1U : 0U,
            metrics->direction_gate ? 1U : 0U,
            metrics->physical_gate ? 1U : 0U,
            metrics->period_gate ? 1U : 0U,
            metrics->waveform_gate ? 1U : 0U,
            metrics->stage_waveform_gate ? 1U : 0U,
            metrics->statistics_gate ? 1U : 0U);

        g_max35103_last_diagnostic_candidate =
            progress.evaluated_candidate_count;
    }

    const bool state_changed =
        progress.state != g_max35103_last_logged_state;
    const bool fallback_changed =
        progress.profile_fallbacks_used !=
            g_max35103_last_logged_fallback;
    if (fallback_changed) {
        MAX35103_AutoCalLog(
            "AUTOCAL|BACKTRACK|from=%s|to=%s"
            "|fallback=%u/%u|finalist=%u/%u\r\n",
            MAX35103_AutoCalStateName(
                g_max35103_last_logged_state),
            MAX35103_AutoCalStateName(progress.state),
            (unsigned)progress.profile_fallbacks_used,
            (unsigned)g_max35103_autocal.config.max_profile_fallbacks,
            (unsigned)(progress.discovery_finalist_index + 1U),
            (unsigned)progress.discovery_finalist_count);
        g_max35103_last_logged_fallback =
            progress.profile_fallbacks_used;
    }
    if (state_changed && !fallback_changed &&
        g_max35103_last_logged_state !=
            MAX35103_AUTOCAL_STATE_IDLE &&
        progress.state != MAX35103_AUTOCAL_STATE_FAILED &&
        progress.state != MAX35103_AUTOCAL_STATE_COMPLETE) {
        const Max35103Profile *selected =
            &g_max35103_autocal.selected_profile;
        MAX35103_AutoCalLog(
            "AUTOCAL|STAGE_PASS|from=%s|to=%s"
            "|TOF1=%04X|DLY=%u\r\n",
            MAX35103_AutoCalStateName(
                g_max35103_last_logged_state),
            MAX35103_AutoCalStateName(progress.state),
            (unsigned)selected->tof1,
            (unsigned)selected->tof_measurement_delay);
    }

    if (!state_changed &&
        progress.stage_retry_count !=
        g_max35103_last_logged_retry) {
        MAX35103_AutoCalLog(
            "AUTOCAL|STAGE_RETRY|state=%s|retry=%u/%u\r\n",
            MAX35103_AutoCalStateName(progress.state),
            (unsigned)progress.stage_retry_count,
            (unsigned)g_max35103_autocal.config.max_stage_retries);
        g_max35103_last_logged_retry =
            progress.stage_retry_count;
    }

    if (state_changed ||
        progress.evaluated_candidate_count >=
            g_max35103_last_logged_candidate +
                MAX35103_AUTOCAL_LOG_CANDIDATE_INTERVAL) {
        MAX35103_AutoCalLog(
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

        g_max35103_last_logged_state = progress.state;
        g_max35103_last_logged_candidate =
            progress.evaluated_candidate_count;
        g_max35103_last_logged_retry =
            progress.stage_retry_count;
    }

    if (status == MAX35103_AUTOCAL_COMPLETE) {
        const Max35103AutoCalStatus report_status =
            MAX35103_AutoCalGetReport(
                &g_max35103_autocal,
                &g_max35103_autocal_report);

        if (report_status == MAX35103_AUTOCAL_COMPLETE) {
            const Max35103Profile *profile =
                &g_max35103_autocal_report.selected_profile;

            MAX35103_AutoCalLog(
                "AUTOCAL|PASS|confidence=%u|valid=%u/1000"
                "|physical=%u/1000|wave=%u/1000"
                "|wvr_up=%u/1000|wvr_dn=%u/1000"
                "|wvr_both=%u/1000|perturb=%u/%u|reset=%u"
                "|fallbacks=%u"
                "|crc=%08lX\r\n",
                (unsigned)g_max35103_autocal_report.confidence,
                (unsigned)g_max35103_autocal_report.verification
                    .valid_rate_per_mille,
                (unsigned)g_max35103_autocal_report.verification
                    .physical_rate_per_mille,
                (unsigned)g_max35103_autocal_report.verification
                    .wave_valid_rate_per_mille,
                (unsigned)g_max35103_autocal_report.verification
                    .wvr_up_good_rate_per_mille,
                (unsigned)g_max35103_autocal_report.verification
                    .wvr_down_good_rate_per_mille,
                (unsigned)g_max35103_autocal_report.verification
                    .wvr_good_rate_per_mille,
                (unsigned)g_max35103_autocal_report.perturbation_passed,
                (unsigned)g_max35103_autocal_report.perturbation_tested,
                g_max35103_autocal_report.reset_verified ? 1U : 0U,
                (unsigned)g_max35103_autocal_report
                    .profile_fallbacks_used,
                (unsigned long)
                    g_max35103_autocal_report.evidence_crc32);

            MAX35103_AutoCalLog(
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

            const Max35103Status configure_status =
                MAX35103_Configure(
                    &g_max35103_driver,
                    &g_max35103_autocal_report.selected_profile);
            if (configure_status == MAX35103_OK) {
                MAX35103_AutoCalLog(
                    "AUTOCAL|CONFIG_APPLIED\r\n");
            } else {
                MAX35103_AutoCalLog(
                    "AUTOCAL|CONFIG_FAIL|status=%d\r\n",
                    (int)configure_status);
            }
        } else {
            MAX35103_AutoCalLog(
                "AUTOCAL|FAIL|report_status=%d\r\n",
                (int)report_status);
        }

        g_max35103_autocal_active = false;
    } else if (status < MAX35103_AUTOCAL_OK) {
        const Max35103AutoCalMetrics *failure =
            &g_max35103_autocal.failure_metrics;
        const Max35103Profile *failure_profile =
            &g_max35103_autocal.failure_profile;
        const uint32_t failure_period_ps =
            failure->median_period_error_ps < 0
                ? 0U
                : failure->median_period_error_ps >
                          (int64_t)UINT32_MAX
                      ? UINT32_MAX
                      : (uint32_t)
                            failure->median_period_error_ps;
        MAX35103_AutoCalLog(
            "AUTOCAL|FAIL|status=%d|state=%s"
            "|candidate=%lu|retry=%u|driver=%d"
            "|TOF1=%04X|DLY=%u\r\n",
            (int)status,
            MAX35103_AutoCalStateName(
                g_max35103_autocal.failure_state),
            (unsigned long)
                g_max35103_autocal.failure_candidate_index,
            (unsigned)g_max35103_autocal.failure_retry_count,
            (int)progress.last_driver_status,
            (unsigned)failure_profile->tof1,
            (unsigned)failure_profile->tof_measurement_delay);
        MAX35103_AutoCalLog(
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
        g_max35103_autocal_active = false;
    }
}
#endif
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_RTC_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */
  /* Run FRAM hardware tests once at startup */
#ifdef FIRMWARE_BUILD_TESTS_FM24CL04B
  FRAM_Test_RunAll();
#endif /* FIRMWARE_BUILD_TESTS */

#ifdef FIRMWARE_BUILD_TESTS_MAX35103
  if (MAX35103_Stm32HalInitTransport(
      &g_max35103_hal_context,
      &hspi1,
      MAX_NSS_GPIO_Port, MAX_NSS_Pin,
      MAX_RST_GPIO_Port, MAX_RST_Pin,
      &g_max35103_transport) != MAX35103_OK)
  {
    Error_Handler();
  }
  MAX35103_Test_RunAll(&g_max35103_transport, &g_max35103_profile);
#endif /* FIRMWARE_BUILD_TESTS_MAX35103 */

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
  const Max35103AutoCalStatus autocal_status =
      MAX35103_AutoCalBoardStart();
  if (autocal_status != MAX35103_AUTOCAL_RUNNING)
  {
    MAX35103_AutoCalLog(
        "AUTOCAL|FAIL|startup_status=%d\r\n",
        (int)autocal_status);
    Error_Handler();
  }
  g_max35103_autocal_active = true;
  MAX35103_AutoCalLog(
      "AUTOCAL|START|path_um=%lu|arrival_ns=%ld..%ld"
      "|dly_ticks=%u..%u|dpl=%u..%u|zero_flow=0\r\n",
      (unsigned long)g_max35103_autocal.config.acoustic_path_length_um,
      (long)(g_max35103_autocal.config.expected_min_tof_ps /
          INT64_C(1000)),
      (long)(g_max35103_autocal.config.expected_max_tof_ps /
          INT64_C(1000)),
      (unsigned)g_max35103_autocal.config.dly_min,
      (unsigned)g_max35103_autocal.config.dly_max,
      (unsigned)g_max35103_autocal.config.dpl_min,
      (unsigned)g_max35103_autocal.config.dpl_max);
  MAX35103_AutoCalLog(
      "AUTOCAL|POLICY|valid_rate=%u"
      "|physical_tuning=%u|physical_verify=%u"
      "|wave_rate=%u|wvr_rate=%u"
      "|wvr_t1_t2_q7=%u..%u|wvr_t2_ideal_q7=%u..%u"
      "|period_max_ps=%lu|direction_delta_max_ns=%lu"
      "|slip_rate=%u|finalist_samples=%u"
      "|stage_retries=%u|profile_fallbacks=%u|busy_polls=%u\r\n",
      (unsigned)g_max35103_autocal.config.min_valid_rate_per_mille,
      (unsigned)g_max35103_autocal.config
          .min_tuning_physical_rate_per_mille,
      (unsigned)g_max35103_autocal.config
          .min_physical_rate_per_mille,
      (unsigned)g_max35103_autocal.config
          .min_wave_valid_rate_per_mille,
      (unsigned)g_max35103_autocal.config
          .min_wvr_good_rate_per_mille,
      (unsigned)g_max35103_autocal.config.wvr_t1_t2_min_q7,
      (unsigned)g_max35103_autocal.config.wvr_ratio_max_q7,
      (unsigned)g_max35103_autocal.config.wvr_t2_ideal_min_q7,
      (unsigned)g_max35103_autocal.config.wvr_ratio_max_q7,
      (unsigned long)g_max35103_autocal.config.max_period_error_ps,
      (unsigned long)(g_max35103_autocal.config
          .max_direction_delta_ps / INT64_C(1000)),
      (unsigned)g_max35103_autocal.config
          .max_cycle_slip_rate_per_mille,
      (unsigned)g_max35103_autocal.config.finalist_samples,
      (unsigned)g_max35103_autocal.config.max_stage_retries,
      (unsigned)g_max35103_autocal.config.max_profile_fallbacks,
      (unsigned)g_max35103_autocal.config.max_busy_polls);
#endif /* FIRMWARE_BUILD_MAX35103_AUTOCAL */

#ifdef FIRMWARE_BUILD_TESTS_ZSSC3241
  ZSSC3241_Test_RunAll(&g_zssc3241_test_config);
#endif /* FIRMWARE_BUILD_TESTS_ZSSC3241 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  //uint32_t mv;
  //uint32_t raw;
  while (1)
  {
    // raw = ADC_Read_Voltage();
    // mv = (raw * 6600) / 4095;  // mV
    // char buffer[50];
    // int len = sprintf(buffer, "ADC Raw: %lu, Voltage: %lu mV\r\n", raw, mv);
    // HAL_UART_Transmit(&huart2, (uint8_t*)buffer, len, HAL_MAX_DELAY);
    // HAL_Delay(100);

#ifdef FIRMWARE_BUILD_MAX35103_AUTOCAL
    if (g_max35103_autocal_active)
    {
      MAX35103_AutoCalPoll();
    }
#endif
    
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 20;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x0070133F;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};
  RTC_AlarmTypeDef sAlarm = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the Alarm A
  */
  sAlarm.AlarmTime.Hours = 0x0;
  sAlarm.AlarmTime.Minutes = 0x0;
  sAlarm.AlarmTime.Seconds = 0x0;
  sAlarm.AlarmTime.SubSeconds = 0x0;
  sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
  sAlarm.AlarmMask = RTC_ALARMMASK_NONE;
  sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
  sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
  sAlarm.AlarmDateWeekDay = 0x1;
  sAlarm.Alarm = RTC_ALARM_A;
  if (HAL_RTC_SetAlarm_IT(&hrtc, &sAlarm, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(MAX_NSS_GPIO_Port, MAX_NSS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(MAX_RST_GPIO_Port, MAX_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(MEASURE_PIN_GPIO_Port, MEASURE_PIN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : MAX_NSS_Pin */
  GPIO_InitStruct.Pin = MAX_NSS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MAX_NSS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MAX_RST_Pin */
  GPIO_InitStruct.Pin = MAX_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MAX_RST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MAX_INT_Pin */
  GPIO_InitStruct.Pin = MAX_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(MAX_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MAX_CMP_Pin */
  GPIO_InitStruct.Pin = MAX_CMP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MAX_CMP_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MAX_WDO_Pin */
  GPIO_InitStruct.Pin = MAX_WDO_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(MAX_WDO_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MEASURE_PIN_Pin */
  GPIO_InitStruct.Pin = MEASURE_PIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MEASURE_PIN_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI2_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
uint32_t ADC_Read_Voltage(void)
{
    uint32_t adc_value = 0;
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY) == HAL_OK)
    {
        adc_value = HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    return adc_value;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
