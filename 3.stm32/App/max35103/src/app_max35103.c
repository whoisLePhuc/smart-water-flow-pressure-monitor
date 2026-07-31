/**
  ******************************************************************************
  * @file    app_max35103.c
  * @brief   Application-level MAX35103 measurement state machine
  ******************************************************************************
  *
  * Contains the application operating states around the MAX35103 driver:
  * run auto-calibration first, then measure TOF + sound speed on a fixed
  * 1 s period, recover the device after consecutive errors and log faults.
  * All UART logging goes through USART2 via APP_UART_Write().
  *
  ******************************************************************************
  */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "max35103.h"
#include "max35103_stm32_hal.h"
#include "max35103_autocal.h"
#include "autocal_board.h"
#include "app_max35103.h"

const Max35103Profile g_max35103_seed_profile = {
    .profile_id = 1U,
    .profile_version = 1U,
    .event_mode_cmd = MAX35103_CMD_EVTMG2,
    .tof1 = 0x1813U,
    .tof2 = 0x4201U,
    .tof3 = 0x0506U,
    .tof4 = 0x0708U,
    .tof5 = 0x090AU,
    .tof6 = 0xFC0DU,
    .tof7 = 0xFC0AU,
    .event_timing_1 = 0x0100U,
    .event_timing_2 = MAX35103_EVT2_TEMP_T1_T3,
    .tof_measurement_delay = 0x0021U,
    .calibration_control = MAX35103_CAL_CTRL_INT_EN,
    .init_timeout_ms = 20U,
    .result_timeout_ms = 20U,
    .halt_timeout_ms = 20U,
    .reference_resistance_milliohm = 1000000U,
    .rtd_nominal_resistance_milliohm = 100000U,
};
Max35103Stm32HalContext g_max35103_hal_context;
Max35103Transport g_max35103_transport;

/** Application-level operating state for MAX35103 calibration and measurement. */
typedef enum
{
    APP_MAX35103_STATE_AUTOCAL = 0,
    APP_MAX35103_STATE_MEASURING,
    APP_MAX35103_STATE_RECOVERING,
    APP_MAX35103_STATE_FAULT
} AppMax35103State;

#define APP_MAX35103_MEASUREMENT_PERIOD_MS      1000U
#define APP_MAX35103_RETRY_DELAY_MS             100U
#define APP_MAX35103_FAULT_LOG_PERIOD_MS        5000U
#define APP_MAX35103_UART_TIMEOUT_MS            100U
#define APP_MAX35103_MAX_CONSECUTIVE_ERRORS     3U
#define APP_MAX35103_ACOUSTIC_PATH_UM           INT64_C(15000)
#define APP_MAX35103_UM_PS_TO_MPS               INT64_C(1000000)
#define APP_MAX35103_Q16_SCALE                  INT64_C(65536)
#define APP_MAX35103_PS_PER_DPL_UNIT            INT64_C(500000)

static Max35103Driver g_max35103_driver;
static Max35103AutoCalReport g_max35103_autocal_report;
static AppMax35103State g_max35103_app_state = APP_MAX35103_STATE_AUTOCAL;
static uint32_t g_max35103_next_action_ms;
static uint32_t g_max35103_last_fault_log_ms;
static uint8_t g_max35103_consecutive_errors;

static bool APP_TickDue(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void APP_UART_Write(const char *text)
{
    if (text == NULL)
    {
        return;
    }

    const size_t length = strlen(text);
    if (length == 0U)
    {
        return;
    }

    const uint16_t tx_length =
        (uint16_t)(length > UINT16_MAX ? UINT16_MAX : length);

    (void)HAL_UART_Transmit(&huart2,
                           (uint8_t *)text,
                           tx_length,
                           APP_MAX35103_UART_TIMEOUT_MS);
}

static void APP_MAX35103_LogStatus(const char *tag, Max35103Status status)
{
    char buffer[96];
    const int length = snprintf(buffer,
                                sizeof(buffer),
                                "MEAS|%s|status=%d|driver_state=%d\r\n",
                                tag != NULL ? tag : "UNKNOWN",
                                (int)status,
                                (int)MAX35103_GetState(&g_max35103_driver));

    if (length > 0)
    {
        APP_UART_Write(buffer);
    }
}

static uint8_t APP_MAX35103_ProfileDpl(const Max35103Profile *profile)
{
    return (uint8_t)((profile->tof1 & MAX35103_TOF1_DPL_MASK) >> 4);
}

static uint8_t APP_MAX35103_ProfileHitWave(const Max35103Profile *profile,
                                           uint8_t hit_index)
{
    const uint16_t words[MAX35103_WAVE_HIT_COUNT / 2U] = {
        profile->tof3,
        profile->tof4,
        profile->tof5,
    };

    const uint16_t word = words[hit_index / 2U];
    uint8_t wave = (hit_index & 1U) == 0U
                       ? (uint8_t)((word >> 8) & MAX35103_TOF_WAVE_SELECT_MASK)
                       : (uint8_t)(word & MAX35103_TOF_WAVE_SELECT_MASK);

    const uint8_t earliest_wave = (uint8_t)(hit_index + 3U);
    if (wave < earliest_wave)
    {
        wave = earliest_wave;
    }

    return wave;
}

static bool APP_MAX35103_ComputeWaveZeroTof(const Max35103Profile *profile,
                                            const Max35103WaveEvidence *wave,
                                            int64_t *tof_up_ps,
                                            int64_t *tof_down_ps)
{
    if (profile == NULL || wave == NULL || tof_up_ps == NULL || tof_down_ps == NULL ||
        !wave->valid || wave->configured_hit_count == 0U ||
        wave->configured_hit_count > MAX35103_WAVE_HIT_COUNT)
    {
        return false;
    }

    const int64_t period_ps =
        ((int64_t)APP_MAX35103_ProfileDpl(profile) + INT64_C(1)) *
        APP_MAX35103_PS_PER_DPL_UNIT;

    int64_t up_sum_ps = 0;
    int64_t down_sum_ps = 0;

    for (uint8_t hit = 0U; hit < wave->configured_hit_count; ++hit)
    {
        const int64_t wave_delay_ps =
            (int64_t)APP_MAX35103_ProfileHitWave(profile, hit) * period_ps;
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

static Max35103Status APP_MAX35103_Measure(Max35103RawResult *result,
                                           Max35103WaveEvidence *wave)
{
    if (result == NULL || wave == NULL)
    {
        return MAX35103_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    memset(wave, 0, sizeof(*wave));

    const Max35103Status measurement_status =
        MAX35103_SelfCheck(&g_max35103_driver);

    Max35103Status mailbox_status = MAX35103_NO_RESULT;

    /*
     * SelfCheck can publish an invalid result before returning an error.
     * Always drain the mailbox so the next measurement is not blocked by
     * result_pending.
     */
    if (MAX35103_HasResult(&g_max35103_driver))
    {
        mailbox_status =
            MAX35103_GetResult(&g_max35103_driver, result);
    }

    if (measurement_status != MAX35103_OK)
    {
        return measurement_status;
    }

    if (mailbox_status != MAX35103_OK)
    {
        return mailbox_status;
    }

    if (!result->valid)
    {
        return MAX35103_DEVICE_ERROR;
    }

    const Max35103Status wave_status =
        MAX35103_ReadWaveEvidence(&g_max35103_driver, wave);

    if (wave_status != MAX35103_OK)
    {
        return wave_status;
    }

    return wave->valid ? MAX35103_OK : MAX35103_DEVICE_ERROR;
}

static bool APP_MAX35103_ComputeSoundSpeedQ16(int64_t tof_up_ps,
                                              int64_t tof_down_ps,
                                              int32_t *speed_q16)
{
    if (speed_q16 == NULL || tof_up_ps <= 0 || tof_down_ps <= 0)
    {
        return false;
    }

    const int64_t numerator_q16 =
        APP_MAX35103_ACOUSTIC_PATH_UM *
        APP_MAX35103_UM_PS_TO_MPS *
        APP_MAX35103_Q16_SCALE;

    const int64_t speed_up_q16 = numerator_q16 / tof_up_ps;
    const int64_t speed_down_q16 = numerator_q16 / tof_down_ps;
    const int64_t mean_speed_q16 =
        speed_up_q16 + (speed_down_q16 - speed_up_q16) / INT64_C(2);

    if (mean_speed_q16 <= 0 || mean_speed_q16 > INT32_MAX)
    {
        return false;
    }

    *speed_q16 = (int32_t)mean_speed_q16;
    return true;
}

static void APP_MAX35103_LogMeasurement(const Max35103RawResult *raw,
                                        const Max35103WaveEvidence *wave,
                                        int64_t zero_up_ps,
                                        int64_t zero_down_ps,
                                        int32_t speed_q16)
{
    if (raw == NULL || wave == NULL)
    {
        return;
    }

    int32_t speed_integer = speed_q16 >> 16;
    uint32_t speed_milli =
        (uint32_t)((((int64_t)(speed_q16 & 0xFFFF) * INT64_C(1000)) + INT64_C(32768)) >> 16);

    if (speed_milli >= 1000U)
    {
        speed_integer++;
        speed_milli -= 1000U;
    }

    const int32_t raw_up_ns = (int32_t)(raw->tof_up_ps / INT64_C(1000));
    const int32_t raw_down_ns = (int32_t)(raw->tof_down_ps / INT64_C(1000));
    const int32_t zero_up_ns = (int32_t)(zero_up_ps / INT64_C(1000));
    const int32_t zero_down_ns = (int32_t)(zero_down_ps / INT64_C(1000));
    const int32_t direction_delta_ns =
        (int32_t)((zero_up_ps - zero_down_ps) / INT64_C(1000));

    char buffer[240];
    const int length = snprintf(
        buffer,
        sizeof(buffer),
        "MEAS|RAW_UP_NS=%ld|RAW_DN_NS=%ld"
        "|ZERO_UP_NS=%ld|ZERO_DN_NS=%ld|DIR_DELTA_NS=%ld"
        "|SPEED_Q16=%ld|SPEED_MPS=%ld.%03lu"
        "|cycles=%u|range=%u|WVR_UP=%u,%u|WVR_DN=%u,%u\r\n",
        (long)raw_up_ns,
        (long)raw_down_ns,
        (long)zero_up_ns,
        (long)zero_down_ns,
        (long)direction_delta_ns,
        (long)speed_q16,
        (long)speed_integer,
        (unsigned long)speed_milli,
        (unsigned)raw->valid_cycle_count,
        (unsigned)raw->tof_range,
        (unsigned)wave->wvr_up_t1_t2_q7,
        (unsigned)wave->wvr_up_t2_ideal_q7,
        (unsigned)wave->wvr_down_t1_t2_q7,
        (unsigned)wave->wvr_down_t2_ideal_q7);

    if (length > 0)
    {
        APP_UART_Write(buffer);
    }
}

static bool APP_MAX35103_Recover(void)
{
    if (MAX35103_ResetDevice(&g_max35103_driver) != MAX35103_OK)
    {
        return false;
    }

    return MAX35103_Configure(
               &g_max35103_driver,
               &g_max35103_autocal_report.selected_profile) == MAX35103_OK;
}

void AppMax35103_Init(void)
{
    g_max35103_app_state = APP_MAX35103_STATE_AUTOCAL;
    g_max35103_consecutive_errors = 0U;
    g_max35103_next_action_ms = HAL_GetTick();
    AUTOCAL_Start(&g_max35103_driver, &g_max35103_seed_profile);
}

void AppMax35103_Run(void)
{
    const uint32_t now_ms = HAL_GetTick();

    switch (g_max35103_app_state)
    {
    case APP_MAX35103_STATE_AUTOCAL: {
        const Max35103AutoCalStatus status = AUTOCAL_Poll();

        if (status == MAX35103_AUTOCAL_COMPLETE)
        {
            if (AUTOCAL_GetReport(&g_max35103_autocal_report))
            {
                g_max35103_consecutive_errors = 0U;
                g_max35103_next_action_ms = now_ms;
                g_max35103_app_state = APP_MAX35103_STATE_MEASURING;
                APP_UART_Write("MEAS|STATE=MEASURING\r\n");
            }
            else
            {
                g_max35103_app_state = APP_MAX35103_STATE_FAULT;
                APP_UART_Write("MEAS|FAULT=AUTOCAL_REPORT_UNAVAILABLE\r\n");
            }
        }
        else if (status < MAX35103_AUTOCAL_OK)
        {
            g_max35103_app_state = APP_MAX35103_STATE_FAULT;
            APP_UART_Write("MEAS|FAULT=AUTOCAL_FAILED\r\n");
        }
        break;
    }

    case APP_MAX35103_STATE_MEASURING: {
        if (!APP_TickDue(now_ms, g_max35103_next_action_ms))
        {
            break;
        }

        g_max35103_next_action_ms =
            now_ms + APP_MAX35103_MEASUREMENT_PERIOD_MS;

        Max35103RawResult raw;
        Max35103WaveEvidence wave;
        const Max35103Status status =
            APP_MAX35103_Measure(&raw, &wave);

        if (status == MAX35103_OK)
        {
            int64_t zero_up_ps = 0;
            int64_t zero_down_ps = 0;
            int32_t speed_q16 = 0;

            const Max35103Profile *profile =
                &g_max35103_autocal_report.selected_profile;

            const bool zero_valid =
                APP_MAX35103_ComputeWaveZeroTof(profile,
                                                &wave,
                                                &zero_up_ps,
                                                &zero_down_ps);

            const bool physical_window_valid =
                zero_valid &&
                zero_up_ps >= g_max35103_autocal_report.expected_min_tof_ps &&
                zero_up_ps <= g_max35103_autocal_report.expected_max_tof_ps &&
                zero_down_ps >= g_max35103_autocal_report.expected_min_tof_ps &&
                zero_down_ps <= g_max35103_autocal_report.expected_max_tof_ps;

            if (!physical_window_valid ||
                !APP_MAX35103_ComputeSoundSpeedQ16(zero_up_ps,
                                                   zero_down_ps,
                                                   &speed_q16))
            {
                APP_MAX35103_LogStatus("INVALID_WAVE_ZERO", MAX35103_OUT_OF_RANGE);
                if (g_max35103_consecutive_errors < UINT8_MAX)
                {
                    g_max35103_consecutive_errors++;
                }
            }
            else
            {
                g_max35103_consecutive_errors = 0U;
                APP_MAX35103_LogMeasurement(&raw,
                                            &wave,
                                            zero_up_ps,
                                            zero_down_ps,
                                            speed_q16);
            }
        }
        else if (status == MAX35103_BUSY)
        {
            APP_MAX35103_LogStatus("BUSY", status);
            g_max35103_next_action_ms = now_ms + APP_MAX35103_RETRY_DELAY_MS;
        }
        else
        {
            APP_MAX35103_LogStatus("READ_FAIL", status);
            if (g_max35103_consecutive_errors < UINT8_MAX)
            {
                g_max35103_consecutive_errors++;
            }
        }

        if (g_max35103_consecutive_errors >=
            APP_MAX35103_MAX_CONSECUTIVE_ERRORS)
        {
            g_max35103_app_state = APP_MAX35103_STATE_RECOVERING;
            APP_UART_Write("MEAS|STATE=RECOVERING\r\n");
        }
        break;
    }

    case APP_MAX35103_STATE_RECOVERING:
        if (!APP_TickDue(now_ms, g_max35103_next_action_ms))
        {
            break;
        }

        if (APP_MAX35103_Recover())
        {
            g_max35103_consecutive_errors = 0U;
            g_max35103_next_action_ms =
                now_ms + APP_MAX35103_MEASUREMENT_PERIOD_MS;
            g_max35103_app_state = APP_MAX35103_STATE_MEASURING;
            APP_UART_Write("MEAS|RECOVERY=PASS\r\n");
        }
        else
        {
            g_max35103_app_state = APP_MAX35103_STATE_FAULT;
            g_max35103_last_fault_log_ms = now_ms;
            APP_UART_Write("MEAS|RECOVERY=FAIL\r\n");
        }
        break;

    case APP_MAX35103_STATE_FAULT:
    default:
        if ((uint32_t)(now_ms - g_max35103_last_fault_log_ms) >=
            APP_MAX35103_FAULT_LOG_PERIOD_MS)
        {
            g_max35103_last_fault_log_ms = now_ms;
            APP_UART_Write("MEAS|STATE=FAULT\r\n");
        }
        break;
    }
}
