/**
  ******************************************************************************
  * @file    max35103.c
  * @brief   Portable MAX35103 time-of-flight and temperature driver
  ******************************************************************************
  */

#include "max35103.h"

#include <string.h>

/* A 4 MHz clock period is 250 ns = 250000 ps. */
#define MAX35103_NOMINAL_CLOCK_PERIOD_PS  INT64_C(250000)
#define MAX35103_Q16_SCALE                INT64_C(65536)
#define MAX35103_COHERENCE_TOLERANCE_Q16  INT64_C(1)

static const uint8_t kResultReadOpcodes[MAX35103_TOF_RESULT_WORDS] = {
    MAX35103_REG_AVGUP_INT,
    MAX35103_REG_AVGUP_FRAC,
    MAX35103_REG_AVGDN_INT,
    MAX35103_REG_AVGDN_FRAC,
    MAX35103_REG_TOF_DIFF_INT,
    MAX35103_REG_TOF_DIFF_FRAC,
    MAX35103_REG_CYCLE_COUNT,
};

static const uint8_t kHitUpIntOpcodes[MAX35103_WAVE_HIT_COUNT] = {
    MAX35103_REG_HIT1UP_INT,
    MAX35103_REG_HIT2UP_INT,
    MAX35103_REG_HIT3UP_INT,
    MAX35103_REG_HIT4UP_INT,
    MAX35103_REG_HIT5UP_INT,
    MAX35103_REG_HIT6UP_INT,
};

static const uint8_t kHitUpFracOpcodes[MAX35103_WAVE_HIT_COUNT] = {
    MAX35103_REG_HIT1UP_FRAC,
    MAX35103_REG_HIT2UP_FRAC,
    MAX35103_REG_HIT3UP_FRAC,
    MAX35103_REG_HIT4UP_FRAC,
    MAX35103_REG_HIT5UP_FRAC,
    MAX35103_REG_HIT6UP_FRAC,
};

static const uint8_t kHitDownIntOpcodes[MAX35103_WAVE_HIT_COUNT] = {
    MAX35103_REG_HIT1DN_INT,
    MAX35103_REG_HIT2DN_INT,
    MAX35103_REG_HIT3DN_INT,
    MAX35103_REG_HIT4DN_INT,
    MAX35103_REG_HIT5DN_INT,
    MAX35103_REG_HIT6DN_INT,
};

static const uint8_t kHitDownFracOpcodes[MAX35103_WAVE_HIT_COUNT] = {
    MAX35103_REG_HIT1DN_FRAC,
    MAX35103_REG_HIT2DN_FRAC,
    MAX35103_REG_HIT3DN_FRAC,
    MAX35103_REG_HIT4DN_FRAC,
    MAX35103_REG_HIT5DN_FRAC,
    MAX35103_REG_HIT6DN_FRAC,
};

static const uint8_t kTemperatureReadOpcodes[MAX35103_TEMP_RESULT_WORDS] = {
    MAX35103_REG_T1_INT,
    MAX35103_REG_T1_FRAC,
    MAX35103_REG_T2_INT,
    MAX35103_REG_T2_FRAC,
    MAX35103_REG_T3_INT,
    MAX35103_REG_T3_FRAC,
    MAX35103_REG_T4_INT,
    MAX35103_REG_T4_FRAC,
};

static const uint8_t kTemperatureAverageReadOpcodes[
    MAX35103_TEMP_RESULT_WORDS] = {
    MAX35103_REG_T1_AVG_INT,
    MAX35103_REG_T1_AVG_FRAC,
    MAX35103_REG_T2_AVG_INT,
    MAX35103_REG_T2_AVG_FRAC,
    MAX35103_REG_T3_AVG_INT,
    MAX35103_REG_T3_AVG_FRAC,
    MAX35103_REG_T4_AVG_INT,
    MAX35103_REG_T4_AVG_FRAC,
};

typedef struct {
    uint8_t write_opcode;
    uint16_t value;
} Max35103ConfigEntry;

/* -------------------------------------------------------------------------- */
/* Injected platform transport                                                */
/* -------------------------------------------------------------------------- */

static bool max_transport_valid(const Max35103Transport *transport)
{
    return transport != NULL &&
           transport->transfer != NULL &&
           transport->get_tick_ms != NULL &&
           transport->delay_ms != NULL;
}

static Max35103TransportStatus max_spi_xfer(
    Max35103Driver *drv, const uint8_t *tx, uint8_t *rx, uint16_t length)
{
    if (drv == NULL || !max_transport_valid(&drv->transport) ||
        tx == NULL || length == 0U) {
        return MAX35103_TRANSPORT_ERROR;
    }
    return drv->transport.transfer(
        drv->transport.context, tx, rx, length, MAX35103_SPI_TIMEOUT_MS);
}

static Max35103TransportStatus max_spi_command(
    Max35103Driver *drv, uint8_t opcode)
{
    return max_spi_xfer(drv, &opcode, NULL, 1U);
}

static Max35103TransportStatus max_spi_read_reg(
    Max35103Driver *drv, uint8_t read_opcode, uint16_t *value)
{
    if (!value) {
        return MAX35103_TRANSPORT_ERROR;
    }

    uint8_t tx[MAX35103_REGISTER_FRAME_BYTES] = { read_opcode, 0U, 0U };
    uint8_t rx[MAX35103_REGISTER_FRAME_BYTES] = { 0U, 0U, 0U };
    Max35103TransportStatus transport_status = max_spi_xfer(
        drv, tx, rx, MAX35103_REGISTER_FRAME_BYTES);
    if (transport_status == MAX35103_TRANSPORT_OK) {
        *value = (uint16_t)(((uint16_t)rx[1] << 8) |
                            (uint16_t)rx[2]);
    }
    return transport_status;
}

static Max35103TransportStatus max_spi_write_reg(
    Max35103Driver *drv, uint8_t write_opcode, uint16_t value)
{
    uint8_t tx[MAX35103_REGISTER_FRAME_BYTES] = {
        write_opcode,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFFU),
    };
    uint8_t rx[MAX35103_REGISTER_FRAME_BYTES] = { 0U, 0U, 0U };
    return max_spi_xfer(drv, tx, rx, MAX35103_REGISTER_FRAME_BYTES);
}

static bool max_is_execution_opcode(uint8_t opcode)
{
    return opcode <= MAX35103_CMD_CALIBRATE;
}

static bool max_is_read_opcode(uint8_t opcode)
{
    return opcode == MAX35103_REG_CONTROL ||
           (opcode >= 0xB0U && opcode <= MAX35103_REG_INT_STATUS);
}

static bool max_is_write_opcode(uint8_t opcode)
{
    return (opcode >= 0x30U && opcode <= 0x43U) || opcode == 0xFFU;
}

static uint8_t max_readback_opcode(uint8_t write_opcode)
{
    return write_opcode == 0xFFU
           ? MAX35103_REG_CONTROL
           : (uint8_t)(write_opcode | 0x80U);
}

/* -------------------------------------------------------------------------- */
/* State, timeout, and mailbox helpers                                        */
/* -------------------------------------------------------------------------- */

static uint32_t max_profile_timeout(uint32_t configured,
                                    uint32_t fallback)
{
    return configured != 0U ? configured : fallback;
}

static uint32_t max_init_timeout(const Max35103Driver *drv)
{
    return drv && drv->profile
           ? max_profile_timeout(drv->profile->init_timeout_ms,
                                 MAX35103_INIT_TIMEOUT_MS)
           : MAX35103_INIT_TIMEOUT_MS;
}

static uint32_t max_result_timeout(const Max35103Driver *drv)
{
    return drv && drv->profile
           ? max_profile_timeout(drv->profile->result_timeout_ms,
                                 MAX35103_RESULT_TIMEOUT_MS)
           : MAX35103_RESULT_TIMEOUT_MS;
}

static uint32_t max_halt_timeout(const Max35103Driver *drv)
{
    return drv && drv->profile
           ? max_profile_timeout(drv->profile->halt_timeout_ms,
                                 MAX35103_HALT_TIMEOUT_MS)
           : MAX35103_HALT_TIMEOUT_MS;
}

static uint32_t max_get_tick_ms(const Max35103Driver *drv)
{
    return drv->transport.get_tick_ms(drv->transport.context);
}

static void max_delay_ms(const Max35103Driver *drv, uint32_t delay_ms)
{
    drv->transport.delay_ms(drv->transport.context, delay_ms);
}

static bool max_tick_expired(const Max35103Driver *drv,
                             uint32_t start_ms, uint32_t timeout_ms)
{
    return (uint32_t)(max_get_tick_ms(drv) - start_ms) >= timeout_ms;
}

static void max_clear_pending_spi(Max35103Driver *drv)
{
    drv->spi_pending = false;
    drv->spi_length = 0U;
    drv->spi_token = 0U;
}

static void max_clear_operation(Max35103Driver *drv)
{
    drv->attempt_start_us = 0U;
    drv->deadline_us = 0U;
    drv->result_word_index = 0U;
    drv->temperature_cycle_word = 0U;
    drv->latched_status = 0U;
    drv->interrupt_timestamp_us = 0U;
    max_clear_pending_spi(drv);
}

static void max_enter_error(Max35103Driver *drv)
{
    drv->error_count++;
    drv->state = MAX35103_STATE_ERROR;
    drv->generation++;
    max_clear_pending_spi(drv);
    drv->deadline_us = 0U;
}

static bool max_event_continuous(const Max35103Driver *drv)
{
    return drv && drv->profile &&
           (drv->profile->calibration_control &
           MAX35103_CAL_CTRL_ET_CONT) != 0U;
}

static uint8_t max_selected_temperature_ports(const Max35103Driver *drv)
{
    if (!drv || !drv->profile) {
        return 0U;
    }

    switch (drv->profile->event_timing_2 &
            MAX35103_EVT2_TEMP_PORT_MASK) {
    case MAX35103_EVT2_TEMP_T1_T3:
        return MAX35103_TEMP_PORT_T1 | MAX35103_TEMP_PORT_T3;
    case MAX35103_EVT2_TEMP_T2_T4:
        return MAX35103_TEMP_PORT_T2 | MAX35103_TEMP_PORT_T4;
    case MAX35103_EVT2_TEMP_T1_T3_T2:
        return MAX35103_TEMP_PORT_T1 | MAX35103_TEMP_PORT_T2 |
               MAX35103_TEMP_PORT_T3;
    case MAX35103_EVT2_TEMP_ALL:
        return MAX35103_TEMP_PORT_T1 | MAX35103_TEMP_PORT_T2 |
               MAX35103_TEMP_PORT_T3 | MAX35103_TEMP_PORT_T4;
    default:
        return 0U;
    }
}

static void max_finish_event_interrupt(Max35103Driver *drv,
                                       uint16_t status)
{
    const uint16_t final_flags = MAX35103_INT_TOF_EVTMG |
                                 MAX35103_INT_TEMP_EVTMG;

    if ((status & final_flags) != 0U && !max_event_continuous(drv)) {
        drv->event_timing_active = false;
    }

    drv->attempt_start_us = 0U;
    drv->deadline_us = 0U;
    drv->result_word_index = 0U;
    drv->temperature_cycle_word = 0U;
    drv->state = drv->event_timing_active
                 ? MAX35103_STATE_EVENT_RUNNING
                 : MAX35103_STATE_IDLE;
}

static bool max_schedule_register_read(Max35103Driver *drv,
                                       uint8_t read_opcode)
{
    if (!drv || drv->spi_pending || !max_is_read_opcode(read_opcode)) {
        return false;
    }

    drv->tx_buf[0] = read_opcode;
    drv->tx_buf[1] = 0U;
    drv->tx_buf[2] = 0U;
    drv->rx_buf[0] = 0U;
    drv->rx_buf[1] = 0U;
    drv->rx_buf[2] = 0U;
    drv->spi_length = MAX35103_REGISTER_FRAME_BYTES;

    drv->next_spi_token++;
    if (drv->next_spi_token == 0U) {
        drv->next_spi_token++;
    }
    drv->spi_token = drv->next_spi_token;
    drv->spi_pending = true;
    return true;
}

static bool max_schedule_temperature_read(Max35103Driver *drv,
                                          bool averaged)
{
    if (!drv) {
        return false;
    }

    if (drv->result_word_index < MAX35103_TEMP_RESULT_WORDS) {
        const uint8_t *opcodes = averaged
            ? kTemperatureAverageReadOpcodes
            : kTemperatureReadOpcodes;
        return max_schedule_register_read(
            drv, opcodes[drv->result_word_index]);
    }
    if (averaged &&
        drv->result_word_index == MAX35103_TEMP_RESULT_WORDS) {
        return max_schedule_register_read(drv,
                                           MAX35103_REG_TEMP_CYCLE_COUNT);
    }
    return false;
}

static bool max_begin_temperature_read(Max35103Driver *drv, bool averaged)
{
    drv->state = MAX35103_STATE_READ_TEMP_RESULT;
    drv->result_word_index = 0U;
    drv->temperature_cycle_word = 0U;
    memset(drv->temperature_frame, 0, sizeof(drv->temperature_frame));
    return max_schedule_temperature_read(drv, averaged);
}

static void max_publish_result(Max35103Driver *drv,
                               const Max35103RawResult *result)
{
    if (drv->result_pending) {
        drv->dropped_result_count++;
        return;
    }

    drv->result = *result;
    drv->result_pending = true;
    if (result->valid) {
        drv->result_count++;
    } else {
        drv->invalid_result_count++;
    }
}

static void max_publish_status_only(Max35103Driver *drv,
                                    uint16_t status,
                                    uint64_t timestamp_us)
{
    Max35103RawResult result;
    memset(&result, 0, sizeof(result));
    result.status_flags = status;
    result.timestamp_us = timestamp_us;
    result.valid = false;
    max_publish_result(drv, &result);
}

static void max_publish_temperature_result(
    Max35103Driver *drv, const Max35103TemperatureResult *result)
{
    if (drv->temperature_result_pending) {
        drv->dropped_temperature_result_count++;
        return;
    }

    drv->temperature_result = *result;
    drv->temperature_result_pending = true;
    if (result->valid) {
        drv->temperature_result_count++;
    } else {
        drv->invalid_temperature_result_count++;
    }
}

static void max_publish_temperature_status_only(Max35103Driver *drv,
                                                uint16_t status,
                                                uint64_t timestamp_us)
{
    Max35103TemperatureResult result;
    memset(&result, 0, sizeof(result));
    result.status_flags = status;
    result.timestamp_us = timestamp_us;
    result.selected_port_mask = max_selected_temperature_ports(drv);
    result.valid = false;
    max_publish_temperature_result(drv, &result);
}

/* -------------------------------------------------------------------------- */
/* Result decode                                                              */
/* -------------------------------------------------------------------------- */

static int64_t max_q16_unsigned_to_ps(uint32_t value)
{
    return ((int64_t)value * MAX35103_NOMINAL_CLOCK_PERIOD_PS) /
           MAX35103_Q16_SCALE;
}

static int64_t max_q16_signed_to_ps(int64_t value)
{
    if (value >= 0) {
        return (value * MAX35103_NOMINAL_CLOCK_PERIOD_PS) /
               MAX35103_Q16_SCALE;
    }

    return -(((-value) * MAX35103_NOMINAL_CLOCK_PERIOD_PS) /
             MAX35103_Q16_SCALE);
}

static bool max_decode_frame(const uint8_t *frame,
                             uint16_t status,
                             uint64_t timestamp_us,
                             bool require_cycle_count,
                             Max35103RawResult *result)
{
    if (!frame || !result) {
        return false;
    }

    memset(result, 0, sizeof(*result));

    result->avg_up_int = ((uint16_t)frame[0] << 8) | frame[1];
    result->avg_up_frac = ((uint16_t)frame[2] << 8) | frame[3];
    result->avg_down_int = ((uint16_t)frame[4] << 8) | frame[5];
    result->avg_down_frac = ((uint16_t)frame[6] << 8) | frame[7];
    result->tof_diff_int = ((uint16_t)frame[8] << 8) | frame[9];
    result->tof_diff_frac = ((uint16_t)frame[10] << 8) | frame[11];
    result->cycle_range_word = ((uint16_t)frame[12] << 8) | frame[13];

    result->tof_range = (uint8_t)(result->cycle_range_word >> 8);
    result->valid_cycle_count = (uint8_t)(result->cycle_range_word & 0xFFU);
    result->status_flags = status;
    result->timestamp_us = timestamp_us;
    result->valid = false;

    result->tof_up_q16 = ((uint32_t)result->avg_up_int << 16) |
                         result->avg_up_frac;
    result->tof_down_q16 = ((uint32_t)result->avg_down_int << 16) |
                           result->avg_down_frac;

    /* Multiplication avoids undefined behaviour from left-shifting a negative. */
    int64_t diff_q16 = (int64_t)(int16_t)result->tof_diff_int *
                       MAX35103_Q16_SCALE + result->tof_diff_frac;
    result->tof_diff_q16 = (int32_t)diff_q16;

    result->tof_up_ps = max_q16_unsigned_to_ps(result->tof_up_q16);
    result->tof_down_ps = max_q16_unsigned_to_ps(result->tof_down_q16);
    result->tof_diff_ps = max_q16_signed_to_ps(diff_q16);

    if ((status & (MAX35103_INT_TIMEOUT | MAX35103_INT_POR)) != 0U) {
        return false;
    }

    /* AVG integer fields are 15-bit positive values. */
    if ((result->avg_up_int & 0x8000U) != 0U ||
        (result->avg_down_int & 0x8000U) != 0U) {
        return false;
    }

    if (result->tof_up_q16 == UINT32_C(0xFFFFFFFF) ||
        result->tof_down_q16 == UINT32_C(0xFFFFFFFF) ||
        (result->tof_diff_int == 0x7FFFU &&
         result->tof_diff_frac == 0xFFFFU)) {
        return false;
    }

    if (require_cycle_count && result->valid_cycle_count == 0U) {
        return false;
    }

    int64_t expected = (int64_t)result->tof_up_q16 -
                       (int64_t)result->tof_down_q16;
    int64_t coherence_error = diff_q16 - expected;
    if (coherence_error < 0) {
        coherence_error = -coherence_error;
    }
    if (coherence_error > MAX35103_COHERENCE_TOLERANCE_Q16) {
        return false;
    }

    result->valid = true;
    return true;
}

static double max_platinum_resistance_milliohm(int32_t temperature_millic,
                                               uint32_t r0_milliohm)
{
    const double a = 3.9083e-3;
    const double b = -5.775e-7;
    const double c = -4.183e-12;
    const double t = (double)temperature_millic / 1000.0;
    double ratio = 1.0 + a * t + b * t * t;

    if (temperature_millic < 0) {
        ratio += c * (t - 100.0) * t * t * t;
    }
    return (double)r0_milliohm * ratio;
}

Max35103Status MAX35103_PlatinumRtdToMilliCelsius(
    uint32_t resistance_milliohm, uint32_t r0_milliohm,
    int32_t *temperature_millicelsius)
{
    if (!temperature_millicelsius || r0_milliohm == 0U) {
        return MAX35103_INVALID_ARG;
    }

    int32_t low = -200000;
    int32_t high = 850000;
    const double measured = (double)resistance_milliohm;
    const double minimum = max_platinum_resistance_milliohm(low,
                                                             r0_milliohm);
    const double maximum = max_platinum_resistance_milliohm(high,
                                                             r0_milliohm);
    if (measured < minimum || measured > maximum) {
        return MAX35103_OUT_OF_RANGE;
    }

    while ((high - low) > 1) {
        const int32_t middle = low + (high - low) / 2;
        const double middle_resistance = max_platinum_resistance_milliohm(
            middle, r0_milliohm);
        if (middle_resistance < measured) {
            low = middle;
        } else {
            high = middle;
        }
    }

    const double low_error = measured - max_platinum_resistance_milliohm(
        low, r0_milliohm);
    const double high_error = max_platinum_resistance_milliohm(
        high, r0_milliohm) - measured;
    *temperature_millicelsius = low_error <= high_error ? low : high;
    return MAX35103_OK;
}

static uint32_t max_ratio_to_resistance(uint32_t sensor_q16,
                                        uint32_t reference_q16,
                                        uint32_t reference_milliohm)
{
    const uint64_t numerator = (uint64_t)sensor_q16 *
                               (uint64_t)reference_milliohm;
    const uint64_t rounded =
        (numerator + (uint64_t)reference_q16 / 2U) /
        (uint64_t)reference_q16;
    return rounded > UINT32_MAX ? UINT32_MAX : (uint32_t)rounded;
}

static bool max_decode_temperature_frame(
    const Max35103Driver *drv, const uint8_t *frame, uint16_t status,
    uint64_t timestamp_us, bool averaged, uint16_t cycle_word,
    Max35103TemperatureResult *result)
{
    if (!drv || !frame || !result) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    result->status_flags = status;
    result->timestamp_us = timestamp_us;
    result->selected_port_mask = max_selected_temperature_ports(drv);
    result->valid_cycle_count = (uint8_t)(cycle_word & 0x00FFU);
    result->averaged = averaged;

    for (uint8_t port = 0U; port < MAX35103_TEMP_PORT_COUNT; ++port) {
        const uint8_t offset = (uint8_t)(port * 4U);
        const uint8_t port_mask = (uint8_t)(1U << port);
        result->port_int[port] = (uint16_t)(
            ((uint16_t)frame[offset] << 8) | frame[offset + 1U]);
        result->port_frac[port] = (uint16_t)(
            ((uint16_t)frame[offset + 2U] << 8) | frame[offset + 3U]);
        result->port_q16[port] =
            ((uint32_t)result->port_int[port] << 16) |
            result->port_frac[port];

        if ((result->selected_port_mask & port_mask) == 0U) {
            continue;
        }
        if (result->port_q16[port] == 0U) {
            result->short_circuit_mask |= port_mask;
            continue;
        }
        if (result->port_q16[port] == UINT32_C(0xFFFFFFFF) ||
            (result->port_int[port] & 0x8000U) != 0U) {
            result->open_circuit_mask |= port_mask;
            continue;
        }
        result->valid_port_mask |= port_mask;
    }

    if (drv->profile &&
        drv->profile->reference_resistance_milliohm != 0U) {
        const uint8_t pair1_mask = MAX35103_TEMP_PORT_T1 |
                                   MAX35103_TEMP_PORT_T3;
        const uint8_t pair2_mask = MAX35103_TEMP_PORT_T2 |
                                   MAX35103_TEMP_PORT_T4;

        if ((result->selected_port_mask & pair1_mask) == pair1_mask &&
            (result->valid_port_mask & pair1_mask) == pair1_mask) {
            result->rtd1_resistance_milliohm = max_ratio_to_resistance(
                result->port_q16[0], result->port_q16[2],
                drv->profile->reference_resistance_milliohm);
            result->rtd1_valid = true;
        }
        if ((result->selected_port_mask & pair2_mask) == pair2_mask &&
            (result->valid_port_mask & pair2_mask) == pair2_mask) {
            result->rtd2_resistance_milliohm = max_ratio_to_resistance(
                result->port_q16[1], result->port_q16[3],
                drv->profile->reference_resistance_milliohm);
            result->rtd2_valid = true;
        }
    }

    if (drv->profile && drv->profile->rtd_nominal_resistance_milliohm != 0U) {
        if (result->rtd1_valid &&
            MAX35103_PlatinumRtdToMilliCelsius(
                result->rtd1_resistance_milliohm,
                drv->profile->rtd_nominal_resistance_milliohm,
                &result->rtd1_temperature_millicelsius) == MAX35103_OK) {
            result->rtd1_temperature_valid = true;
        }
        if (result->rtd2_valid &&
            MAX35103_PlatinumRtdToMilliCelsius(
                result->rtd2_resistance_milliohm,
                drv->profile->rtd_nominal_resistance_milliohm,
                &result->rtd2_temperature_millicelsius) == MAX35103_OK) {
            result->rtd2_temperature_valid = true;
        }
    }

    result->valid = result->selected_port_mask != 0U &&
                    (result->valid_port_mask & result->selected_port_mask) ==
                        result->selected_port_mask &&
                    (status & (MAX35103_INT_TIMEOUT |
                               MAX35103_INT_POR)) == 0U &&
                    (!averaged || result->valid_cycle_count != 0U);
    return result->valid;
}

static Max35103Status max_read_tof_words_blocking(
    Max35103Driver *drv,
    uint16_t status,
    uint64_t timestamp_us,
    bool require_cycle_count,
    Max35103RawResult *result)
{
    uint8_t frame[MAX35103_TOF_RESULT_FRAME_BYTES];

    for (uint8_t i = 0U; i < MAX35103_TOF_RESULT_WORDS; ++i) {
        uint16_t word = 0U;
        if (max_spi_read_reg(drv, kResultReadOpcodes[i], &word) !=
            MAX35103_TRANSPORT_OK) {
            return MAX35103_SPI_ERROR;
        }
        frame[i * 2U] = (uint8_t)(word >> 8);
        frame[i * 2U + 1U] = (uint8_t)(word & 0xFFU);
    }

    return max_decode_frame(frame, status, timestamp_us,
                            require_cycle_count, result)
           ? MAX35103_OK
           : MAX35103_DEVICE_ERROR;
}

static Max35103Status max_read_temperature_words_blocking(
    Max35103Driver *drv, uint16_t status, uint64_t timestamp_us,
    bool averaged, Max35103TemperatureResult *result)
{
    uint8_t frame[MAX35103_TEMP_RESULT_FRAME_BYTES];
    const uint8_t *opcodes = averaged ? kTemperatureAverageReadOpcodes
                                      : kTemperatureReadOpcodes;

    for (uint8_t i = 0U; i < MAX35103_TEMP_RESULT_WORDS; ++i) {
        uint16_t word = 0U;
        if (max_spi_read_reg(drv, opcodes[i], &word) !=
            MAX35103_TRANSPORT_OK) {
            return MAX35103_SPI_ERROR;
        }
        frame[i * 2U] = (uint8_t)(word >> 8);
        frame[i * 2U + 1U] = (uint8_t)(word & 0xFFU);
    }

    uint16_t cycle_word = 0U;
    if (averaged &&
        max_spi_read_reg(drv, MAX35103_REG_TEMP_CYCLE_COUNT,
                         &cycle_word) != MAX35103_TRANSPORT_OK) {
        return MAX35103_SPI_ERROR;
    }

    return max_decode_temperature_frame(drv, frame, status, timestamp_us,
                                        averaged, cycle_word, result)
           ? MAX35103_OK
           : MAX35103_DEVICE_ERROR;
}

static uint8_t max_effective_t2_wave(const Max35103Profile *profile)
{
    uint8_t wave = (uint8_t)(
        (profile->tof2 & MAX35103_TOF2_T2WV_MASK) >>
        MAX35103_TOF2_T2WV_SHIFT);
    return wave < 2U ? 2U : wave;
}

static uint8_t max_effective_hit_wave(
    const Max35103Profile *profile, uint8_t hit_index)
{
    const uint16_t words[MAX35103_WAVE_HIT_COUNT / 2U] = {
        profile->tof3,
        profile->tof4,
        profile->tof5,
    };
    const uint16_t word = words[hit_index / 2U];
    uint8_t wave = (hit_index & 1U) == 0U
                   ? (uint8_t)((word >> 8) &
                               MAX35103_TOF_WAVE_SELECT_MASK)
                   : (uint8_t)(word &
                               MAX35103_TOF_WAVE_SELECT_MASK);
    const uint8_t earliest = (uint8_t)(hit_index + 3U);
    return wave < earliest ? earliest : wave;
}

/* -------------------------------------------------------------------------- */
/* Public control API                                                         */
/* -------------------------------------------------------------------------- */

Max35103Status MAX35103_Init(
    Max35103Driver *drv, const Max35103Transport *transport)
{
    if (drv == NULL) {
        return MAX35103_INVALID_ARG;
    }

    memset(drv, 0, sizeof(*drv));
    if (!max_transport_valid(transport)) {
        drv->state = MAX35103_STATE_UNINIT;
        return MAX35103_INVALID_ARG;
    }

    drv->transport = *transport;
    drv->state = MAX35103_STATE_IDLE;
    drv->generation = 1U;
    return MAX35103_OK;
}

Max35103Status MAX35103_ResetDevice(Max35103Driver *drv)
{
    if (!drv) {
        return MAX35103_INVALID_ARG;
    }
    if (drv->state == MAX35103_STATE_UNINIT) {
        return MAX35103_NOT_READY;
    }

    drv->generation++;
    drv->state = MAX35103_STATE_ARMING;
    drv->device_ready = false;
    drv->configured = false;
    drv->event_timing_active = false;
    drv->result_pending = false;
    drv->temperature_result_pending = false;
    memset(&drv->result, 0, sizeof(drv->result));
    memset(&drv->temperature_result, 0, sizeof(drv->temperature_result));
    max_clear_operation(drv);

    if (drv->transport.set_reset == NULL) {
        max_enter_error(drv);
        return MAX35103_NOT_READY;
    }
    if (drv->transport.set_reset(
            drv->transport.context, true) != MAX35103_TRANSPORT_OK) {
        max_enter_error(drv);
        return MAX35103_SPI_ERROR;
    }
    max_delay_ms(drv, MAX35103_RESET_PULSE_MS);
    if (drv->transport.set_reset(
            drv->transport.context, false) != MAX35103_TRANSPORT_OK) {
        max_enter_error(drv);
        return MAX35103_SPI_ERROR;
    }
    max_delay_ms(drv, MAX35103_RESET_READY_MS);

    uint16_t status = 0U;
    if (max_spi_read_reg(drv, MAX35103_REG_INT_STATUS, &status) !=
        MAX35103_TRANSPORT_OK) {
        max_enter_error(drv);
        return MAX35103_SPI_ERROR;
    }
    if (status == 0xFFFFU || (status & MAX35103_INT_POR) == 0U) {
        max_enter_error(drv);
        return MAX35103_DEVICE_ERROR;
    }

    if (max_spi_command(drv, MAX35103_CMD_INIT) !=
        MAX35103_TRANSPORT_OK) {
        max_enter_error(drv);
        return MAX35103_SPI_ERROR;
    }

    const uint32_t timeout_ms = max_init_timeout(drv);
    const uint32_t start_ms = max_get_tick_ms(drv);

    /* INIT typically takes 2.5 ms and SPI is unavailable while it executes. */
    max_delay_ms(drv, MAX35103_INIT_SETTLE_MS);

    while (!max_tick_expired(drv, start_ms, timeout_ms)) {
        status = 0U;
        if (max_spi_read_reg(drv, MAX35103_REG_INT_STATUS, &status) !=
            MAX35103_TRANSPORT_OK) {
            max_enter_error(drv);
            return MAX35103_SPI_ERROR;
        }
        if (status != 0xFFFFU &&
            (status & MAX35103_INT_INIT_COMPLETE) != 0U) {
            drv->device_ready = true;
            drv->state = MAX35103_STATE_IDLE;
            max_clear_operation(drv);
            return MAX35103_OK;
        }
        max_delay_ms(drv, 1U);
    }

    drv->timeout_count++;
    drv->error_count++;
    drv->state = MAX35103_STATE_TIMEOUT;
    return MAX35103_TIMEOUT;
}

Max35103Status MAX35103_ValidateProfile(
    const Max35103Profile *profile)
{
    if (!profile) {
        return MAX35103_INVALID_ARG;
    }

    if (profile->event_mode_cmd != MAX35103_CMD_EVTMG1 &&
        profile->event_mode_cmd != MAX35103_CMD_EVTMG2 &&
        profile->event_mode_cmd != MAX35103_CMD_EVTMG3) {
        return MAX35103_CONFIG_ERROR;
    }

    if ((profile->tof1 & MAX35103_TOF1_PL_MASK) == 0U ||
        (profile->tof1 & MAX35103_TOF1_DPL_MASK) == 0U ||
        (profile->tof1 & MAX35103_TOF1_RESERVED_MASK) != 0U ||
        (profile->tof2 & MAX35103_TOF2_RESERVED_MASK) != 0U ||
        ((profile->tof3 | profile->tof4 | profile->tof5) &
         MAX35103_TOF3_5_RESERVED_MASK) != 0U ||
        ((profile->tof6 | profile->tof7) &
         MAX35103_TOF6_7_RESERVED_MASK) != 0U ||
        profile->tof_measurement_delay < MAX35103_TOF_DELAY_MIN ||
        (profile->calibration_control &
         MAX35103_CAL_CTRL_RESERVED_MASK) != 0U) {
        return MAX35103_CONFIG_ERROR;
    }

    /*
     * TIMOUT = 128 us * 2^code. One DLY tick is one 4 MHz period
     * (0.25 us), so the timeout expressed in DLY ticks is 512 * 2^code.
     */
    const uint32_t timeout_ticks =
        UINT32_C(512) <<
        (profile->tof2 & MAX35103_TOF2_TIMEOUT_MASK);
    if ((uint32_t)profile->tof_measurement_delay > timeout_ticks) {
        return MAX35103_CONFIG_ERROR;
    }

    /*
     * Validate the effective wave sequence. The device clamps low selectors
     * to their earliest legal wave, so compare those effective values rather
     * than rejecting reset-compatible zero fields.
     */
    uint8_t previous_wave = max_effective_t2_wave(profile);
    const uint8_t hit_count = MAX35103_ConfiguredHitCount(profile);
    for (uint8_t hit = 0U; hit < hit_count; ++hit) {
        const uint8_t wave = max_effective_hit_wave(profile, hit);
        if (wave <= previous_wave) {
            return MAX35103_CONFIG_ERROR;
        }
        previous_wave = wave;
    }

    return MAX35103_OK;
}

Max35103Status MAX35103_Configure(Max35103Driver *drv,
                                  const Max35103Profile *profile)
{
    if (!drv || !profile) {
        return MAX35103_INVALID_ARG;
    }

    const Max35103Status validation =
        MAX35103_ValidateProfile(profile);
    if (validation != MAX35103_OK) {
        return validation;
    }

    if (!drv->device_ready) {
        return MAX35103_NOT_READY;
    }
    if (drv->state != MAX35103_STATE_IDLE || drv->event_timing_active ||
        drv->spi_pending) {
        return MAX35103_BUSY;
    }
    const Max35103ConfigEntry entries[] = {
        { MAX35103_REG_TOF1, profile->tof1 },
        { MAX35103_REG_TOF2, profile->tof2 },
        { MAX35103_REG_TOF3, profile->tof3 },
        { MAX35103_REG_TOF4, profile->tof4 },
        { MAX35103_REG_TOF5, profile->tof5 },
        { MAX35103_REG_TOF6, profile->tof6 },
        { MAX35103_REG_TOF7, profile->tof7 },
        { MAX35103_REG_EVT_TIMING_1, profile->event_timing_1 },
        { MAX35103_REG_EVT_TIMING_2, profile->event_timing_2 },
        { MAX35103_REG_TOF_MEAS_DELAY, profile->tof_measurement_delay },
        { MAX35103_REG_CAL_CTRL, profile->calibration_control },
    };

    drv->configured = false;
    for (uint8_t i = 0U;
         i < (uint8_t)(sizeof(entries) / sizeof(entries[0]));
         ++i) {
        if (max_spi_write_reg(drv, entries[i].write_opcode,
                              entries[i].value) !=
            MAX35103_TRANSPORT_OK) {
            max_enter_error(drv);
            return MAX35103_SPI_ERROR;
        }

        uint16_t readback = 0U;
        if (max_spi_read_reg(
                drv, max_readback_opcode(entries[i].write_opcode),
                &readback) != MAX35103_TRANSPORT_OK) {
            max_enter_error(drv);
            return MAX35103_SPI_ERROR;
        }
        if (readback != entries[i].value) {
            max_enter_error(drv);
            return MAX35103_CONFIG_ERROR;
        }
    }

    drv->profile = profile;
    drv->configured = true;
    return MAX35103_OK;
}

Max35103Status MAX35103_StartEventTiming(Max35103Driver *drv)
{
    if (!drv) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->device_ready || !drv->configured || !drv->profile) {
        return MAX35103_NOT_READY;
    }
    if (drv->state != MAX35103_STATE_IDLE || drv->event_timing_active ||
        drv->spi_pending || drv->result_pending ||
        drv->temperature_result_pending) {
        return MAX35103_BUSY;
    }
    if (!max_is_execution_opcode(drv->profile->event_mode_cmd)) {
        return MAX35103_CONFIG_ERROR;
    }
    if ((drv->profile->calibration_control &
         MAX35103_CAL_CTRL_INT_EN) == 0U) {
        return MAX35103_CONFIG_ERROR;
    }

    if (max_spi_command(drv, drv->profile->event_mode_cmd) !=
        MAX35103_TRANSPORT_OK) {
        max_enter_error(drv);
        return MAX35103_SPI_ERROR;
    }

    max_clear_operation(drv);
    drv->event_timing_active = true;
    drv->state = MAX35103_STATE_EVENT_RUNNING;
    return MAX35103_OK;
}

Max35103Status MAX35103_Halt(Max35103Driver *drv)
{
    if (!drv) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->device_ready) {
        return MAX35103_NOT_READY;
    }
    if (!drv->event_timing_active) {
        if (drv->state != MAX35103_STATE_ERROR) {
            drv->state = MAX35103_STATE_IDLE;
        }
        return MAX35103_OK;
    }

    drv->generation++;
    max_clear_pending_spi(drv);
    drv->deadline_us = 0U;
    drv->state = MAX35103_STATE_HALTING;

    if (max_spi_command(drv, MAX35103_CMD_HALT) !=
        MAX35103_TRANSPORT_OK) {
        max_enter_error(drv);
        return MAX35103_SPI_ERROR;
    }

    const uint32_t timeout_ms = max_halt_timeout(drv);
    const uint32_t start_ms = max_get_tick_ms(drv);

    while (!max_tick_expired(drv, start_ms, timeout_ms)) {
        uint16_t status = 0U;
        if (max_spi_read_reg(drv, MAX35103_REG_INT_STATUS, &status) !=
            MAX35103_TRANSPORT_OK) {
            max_enter_error(drv);
            return MAX35103_SPI_ERROR;
        }
        if (status != 0xFFFFU &&
            (status & MAX35103_INT_HALT_COMPLETE) != 0U) {
            drv->event_timing_active = false;
            drv->state = MAX35103_STATE_IDLE;
            max_clear_operation(drv);
            return MAX35103_OK;
        }
        if ((status & MAX35103_INT_POR) != 0U) {
            drv->device_ready = false;
            drv->configured = false;
            drv->event_timing_active = false;
            max_enter_error(drv);
            return MAX35103_DEVICE_ERROR;
        }
        max_delay_ms(drv, 1U);
    }

    drv->timeout_count++;
    drv->error_count++;
    drv->state = MAX35103_STATE_TIMEOUT;
    return MAX35103_TIMEOUT;
}

Max35103Status MAX35103_SelfCheck(Max35103Driver *drv)
{
    if (!drv) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->device_ready || !drv->configured) {
        return MAX35103_NOT_READY;
    }
    if (drv->state != MAX35103_STATE_IDLE || drv->event_timing_active ||
        drv->spi_pending || drv->result_pending) {
        return MAX35103_BUSY;
    }

    drv->state = MAX35103_STATE_SELF_CHECK;
    if (max_spi_command(drv, MAX35103_CMD_TOF_DIFF) !=
        MAX35103_TRANSPORT_OK) {
        max_enter_error(drv);
        return MAX35103_SPI_ERROR;
    }

    const uint32_t timeout_ms = max_result_timeout(drv);
    const uint32_t start_ms = max_get_tick_ms(drv);

    while (!max_tick_expired(drv, start_ms, timeout_ms)) {
        uint16_t status = 0U;
        if (max_spi_read_reg(drv, MAX35103_REG_INT_STATUS, &status) !=
            MAX35103_TRANSPORT_OK) {
            max_enter_error(drv);
            return MAX35103_SPI_ERROR;
        }

        if ((status & MAX35103_INT_TOF_COMPLETE) != 0U) {
            Max35103RawResult result;
            Max35103Status read_status = max_read_tof_words_blocking(
                drv, status, 0U, false, &result);

            if (read_status == MAX35103_SPI_ERROR) {
                max_enter_error(drv);
                return read_status;
            }

            max_publish_result(drv, &result);
            drv->state = MAX35103_STATE_IDLE;
            max_clear_operation(drv);
            return result.valid ? MAX35103_OK : MAX35103_DEVICE_ERROR;
        }

        if ((status & (MAX35103_INT_TIMEOUT | MAX35103_INT_POR)) != 0U) {
            max_publish_status_only(drv, status, 0U);
            if ((status & MAX35103_INT_POR) != 0U) {
                drv->device_ready = false;
                drv->configured = false;
            }
            max_enter_error(drv);
            return MAX35103_DEVICE_ERROR;
        }

        max_delay_ms(drv, 1U);
    }

    drv->timeout_count++;
    drv->error_count++;
    drv->state = MAX35103_STATE_TIMEOUT;
    return MAX35103_TIMEOUT;
}

Max35103Status MAX35103_MeasureTemperature(
    Max35103Driver *drv, Max35103TemperatureResult *result)
{
    if (!drv || !result) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->device_ready || !drv->configured || !drv->profile) {
        return MAX35103_NOT_READY;
    }
    if (drv->state != MAX35103_STATE_IDLE || drv->event_timing_active ||
        drv->spi_pending) {
        return MAX35103_BUSY;
    }

    memset(result, 0, sizeof(*result));
    drv->state = MAX35103_STATE_TEMP_MEASURING;
    if (max_spi_command(drv, MAX35103_CMD_TEMPERATURE) !=
        MAX35103_TRANSPORT_OK) {
        max_enter_error(drv);
        return MAX35103_SPI_ERROR;
    }

    const uint32_t timeout_ms = max_result_timeout(drv);
    const uint32_t start_ms = max_get_tick_ms(drv);

    while (!max_tick_expired(drv, start_ms, timeout_ms)) {
        uint16_t status = 0U;
        if (max_spi_read_reg(drv, MAX35103_REG_INT_STATUS, &status) !=
            MAX35103_TRANSPORT_OK) {
            max_enter_error(drv);
            return MAX35103_SPI_ERROR;
        }
        if (status == 0xFFFFU) {
            max_enter_error(drv);
            return MAX35103_DEVICE_ERROR;
        }

        if ((status & MAX35103_INT_TEMP_COMPLETE) != 0U) {
            const Max35103Status read_status =
                max_read_temperature_words_blocking(
                    drv, status, 0U, false, result);
            if (read_status == MAX35103_SPI_ERROR) {
                max_enter_error(drv);
                return read_status;
            }
            drv->state = MAX35103_STATE_IDLE;
            max_clear_operation(drv);
            return read_status;
        }

        if ((status & (MAX35103_INT_TIMEOUT | MAX35103_INT_POR)) != 0U) {
            if ((status & MAX35103_INT_POR) != 0U) {
                result->status_flags = status;
                result->selected_port_mask =
                    max_selected_temperature_ports(drv);
                drv->device_ready = false;
                drv->configured = false;
                max_enter_error(drv);
            } else {
                const Max35103Status read_status =
                    max_read_temperature_words_blocking(
                        drv, status, 0U, false, result);
                if (read_status == MAX35103_SPI_ERROR) {
                    max_enter_error(drv);
                    return read_status;
                }
                drv->state = MAX35103_STATE_IDLE;
                max_clear_operation(drv);
            }
            return MAX35103_DEVICE_ERROR;
        }

        max_delay_ms(drv, 1U);
    }

    drv->timeout_count++;
    drv->error_count++;
    drv->state = MAX35103_STATE_TIMEOUT;
    return MAX35103_TIMEOUT;
}

void MAX35103_Cancel(Max35103Driver *drv)
{
    if (!drv || drv->state == MAX35103_STATE_UNINIT) {
        return;
    }

    drv->generation++;
    max_clear_operation(drv);
    drv->result_pending = false;
    drv->result.valid = false;
    drv->temperature_result_pending = false;
    drv->temperature_result.valid = false;
    drv->state = drv->event_timing_active
                 ? MAX35103_STATE_EVENT_RUNNING
                 : MAX35103_STATE_IDLE;
}

/* -------------------------------------------------------------------------- */
/* Deferred interrupt / SPI FSM                                               */
/* -------------------------------------------------------------------------- */

void MAX35103_OnInt(Max35103Driver *drv, uint64_t now_us)
{
    if (!drv) {
        return;
    }

    drv->irq_count++;
    if (!drv->device_ready || !drv->event_timing_active ||
        drv->state != MAX35103_STATE_EVENT_RUNNING || drv->spi_pending) {
        drv->unexpected_irq_count++;
        return;
    }

    drv->state = MAX35103_STATE_DRAIN_STATUS;
    drv->attempt_start_us = now_us;
    drv->interrupt_timestamp_us = now_us;
    drv->deadline_us = now_us +
                       (uint64_t)max_result_timeout(drv) * UINT64_C(1000);
    drv->latched_status = 0U;
    drv->result_word_index = 0U;

    if (!max_schedule_register_read(drv, MAX35103_REG_INT_STATUS)) {
        max_enter_error(drv);
    }
}

bool MAX35103_GetPendingSpiRequest(Max35103Driver *drv,
                                   Max35103SpiRequest *request)
{
    if (!drv || !request || !drv->spi_pending) {
        return false;
    }

    request->tx = drv->tx_buf;
    request->rx = drv->rx_buf;
    request->length = drv->spi_length;
    request->token = drv->spi_token;
    return true;
}

void MAX35103_OnSpiDone(Max35103Driver *drv, uint32_t token,
                        bool transfer_ok)
{
    if (!drv) {
        return;
    }
    if (!drv->spi_pending || token == 0U || token != drv->spi_token) {
        drv->stale_spi_completion_count++;
        return;
    }

    max_clear_pending_spi(drv);
    drv->spi_done_count++;

    if (!transfer_ok) {
        max_enter_error(drv);
        return;
    }

    switch (drv->state) {
    case MAX35103_STATE_DRAIN_STATUS: {
        const uint16_t status = (uint16_t)(
            ((uint16_t)drv->rx_buf[1] << 8) |
            (uint16_t)drv->rx_buf[2]);
        drv->latched_status = status;

        if (status == 0xFFFFU) {
            max_enter_error(drv);
            return;
        }
        if ((status & MAX35103_INT_POR) != 0U) {
            drv->device_ready = false;
            drv->configured = false;
            drv->event_timing_active = false;
            if (drv->profile &&
                drv->profile->event_mode_cmd != MAX35103_CMD_EVTMG3) {
                max_publish_status_only(drv, status,
                                        drv->interrupt_timestamp_us);
            }
            if (drv->profile &&
                drv->profile->event_mode_cmd != MAX35103_CMD_EVTMG2) {
                max_publish_temperature_status_only(
                    drv, status, drv->interrupt_timestamp_us);
            }
            max_enter_error(drv);
            return;
        }

        const uint16_t tof_ready = MAX35103_INT_TOF_COMPLETE |
                                   MAX35103_INT_TOF_EVTMG;
        const uint16_t temperature_ready = MAX35103_INT_TEMP_COMPLETE |
                                            MAX35103_INT_TEMP_EVTMG;
        if ((status & tof_ready) != 0U) {
            drv->state = MAX35103_STATE_READ_RESULT;
            drv->result_word_index = 0U;
            memset(drv->result_frame, 0, sizeof(drv->result_frame));
            if (!max_schedule_register_read(
                    drv, kResultReadOpcodes[drv->result_word_index])) {
                max_enter_error(drv);
            }
            return;
        }
        if ((status & temperature_ready) != 0U) {
            const bool averaged =
                (status & MAX35103_INT_TEMP_EVTMG) != 0U;
            if (!max_begin_temperature_read(drv, averaged)) {
                max_enter_error(drv);
            }
            return;
        }

        if ((status & MAX35103_INT_TIMEOUT) != 0U) {
            if (drv->profile &&
                drv->profile->event_mode_cmd != MAX35103_CMD_EVTMG3) {
                max_publish_status_only(drv, status,
                                        drv->interrupt_timestamp_us);
            }
            if (drv->profile &&
                drv->profile->event_mode_cmd != MAX35103_CMD_EVTMG2) {
                max_publish_temperature_status_only(
                    drv, status, drv->interrupt_timestamp_us);
            }
        }
        max_finish_event_interrupt(drv, status);
        return;
    }

    case MAX35103_STATE_READ_RESULT: {
        const uint8_t index = drv->result_word_index;
        if (index >= MAX35103_TOF_RESULT_WORDS) {
            max_enter_error(drv);
            return;
        }

        drv->result_frame[index * 2U] = drv->rx_buf[1];
        drv->result_frame[index * 2U + 1U] = drv->rx_buf[2];
        drv->result_word_index++;

        if (drv->result_word_index < MAX35103_TOF_RESULT_WORDS) {
            if (!max_schedule_register_read(
                    drv, kResultReadOpcodes[drv->result_word_index])) {
                max_enter_error(drv);
            }
            return;
        }

        Max35103RawResult decoded;
        (void)max_decode_frame(drv->result_frame,
                               drv->latched_status,
                               drv->interrupt_timestamp_us,
                               true,
                               &decoded);
        max_publish_result(drv, &decoded);

        const uint16_t temperature_ready = MAX35103_INT_TEMP_COMPLETE |
                                            MAX35103_INT_TEMP_EVTMG;
        if ((drv->latched_status & temperature_ready) != 0U) {
            const bool averaged =
                (drv->latched_status & MAX35103_INT_TEMP_EVTMG) != 0U;
            if (!max_begin_temperature_read(drv, averaged)) {
                max_enter_error(drv);
            }
            return;
        }
        max_finish_event_interrupt(drv, drv->latched_status);
        return;
    }

    case MAX35103_STATE_READ_TEMP_RESULT: {
        const bool averaged =
            (drv->latched_status & MAX35103_INT_TEMP_EVTMG) != 0U;
        const uint8_t index = drv->result_word_index;

        if (index < MAX35103_TEMP_RESULT_WORDS) {
            drv->temperature_frame[index * 2U] = drv->rx_buf[1];
            drv->temperature_frame[index * 2U + 1U] = drv->rx_buf[2];
            drv->result_word_index++;

            if (drv->result_word_index < MAX35103_TEMP_RESULT_WORDS ||
                averaged) {
                if (!max_schedule_temperature_read(drv, averaged)) {
                    max_enter_error(drv);
                }
                return;
            }
        } else if (averaged &&
                   index == MAX35103_TEMP_RESULT_WORDS) {
            drv->temperature_cycle_word = (uint16_t)(
                ((uint16_t)drv->rx_buf[1] << 8) |
                (uint16_t)drv->rx_buf[2]);
            drv->result_word_index++;
        } else {
            max_enter_error(drv);
            return;
        }

        Max35103TemperatureResult decoded;
        (void)max_decode_temperature_frame(
            drv, drv->temperature_frame, drv->latched_status,
            drv->interrupt_timestamp_us, averaged,
            drv->temperature_cycle_word, &decoded);
        max_publish_temperature_result(drv, &decoded);
        max_finish_event_interrupt(drv, drv->latched_status);
        return;
    }

    default:
        max_enter_error(drv);
        return;
    }
}

Max35103Status MAX35103_ExecuteSpi(Max35103Driver *drv)
{
    Max35103SpiRequest request;
    if (!MAX35103_GetPendingSpiRequest(drv, &request)) {
        return drv ? MAX35103_NOT_READY : MAX35103_INVALID_ARG;
    }

    const Max35103TransportStatus transport_status = max_spi_xfer(
        drv, request.tx, request.rx, request.length);
    MAX35103_OnSpiDone(
        drv, request.token,
        transport_status == MAX35103_TRANSPORT_OK);
    return transport_status == MAX35103_TRANSPORT_OK
           ? MAX35103_OK
           : MAX35103_SPI_ERROR;
}

Max35103Status MAX35103_Process(Max35103Driver *drv, uint64_t now_us)
{
    if (!drv) {
        return MAX35103_INVALID_ARG;
    }

    if ((drv->state == MAX35103_STATE_DRAIN_STATUS ||
         drv->state == MAX35103_STATE_READ_RESULT ||
         drv->state == MAX35103_STATE_READ_TEMP_RESULT) &&
        drv->deadline_us != 0U && now_us >= drv->deadline_us) {
        MAX35103_OnTimeout(drv);
        return MAX35103_TIMEOUT;
    }

    if (drv->spi_pending) {
        return MAX35103_ExecuteSpi(drv);
    }
    if (drv->state == MAX35103_STATE_TIMEOUT) {
        return MAX35103_TIMEOUT;
    }
    if (drv->state == MAX35103_STATE_ERROR) {
        return MAX35103_DEVICE_ERROR;
    }
    return MAX35103_OK;
}

void MAX35103_OnTimeout(Max35103Driver *drv)
{
    if (!drv ||
        (drv->state != MAX35103_STATE_DRAIN_STATUS &&
         drv->state != MAX35103_STATE_READ_RESULT &&
         drv->state != MAX35103_STATE_READ_TEMP_RESULT)) {
        return;
    }

    const bool was_temperature_read =
        drv->state == MAX35103_STATE_READ_TEMP_RESULT;
    drv->timeout_count++;
    drv->error_count++;
    drv->generation++;
    max_clear_pending_spi(drv);
    drv->deadline_us = 0U;
    drv->state = MAX35103_STATE_TIMEOUT;
    const uint16_t status = (uint16_t)(drv->latched_status |
                                       MAX35103_INT_TIMEOUT);
    if (was_temperature_read) {
        max_publish_temperature_status_only(
            drv, status, drv->interrupt_timestamp_us);
    } else {
        max_publish_status_only(drv, status,
                                drv->interrupt_timestamp_us);
    }
}

/* -------------------------------------------------------------------------- */
/* Blocking diagnostics                                                       */
/* -------------------------------------------------------------------------- */

Max35103Status MAX35103_ReadReg(Max35103Driver *drv,
                                uint8_t read_opcode, uint16_t *value)
{
    if (!drv || !value || !max_is_read_opcode(read_opcode)) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->device_ready) {
        return MAX35103_NOT_READY;
    }
    if (drv->spi_pending || drv->state == MAX35103_STATE_DRAIN_STATUS ||
        drv->state == MAX35103_STATE_READ_RESULT ||
        drv->state == MAX35103_STATE_READ_TEMP_RESULT) {
        return MAX35103_BUSY;
    }

    return max_spi_read_reg(drv, read_opcode, value) ==
               MAX35103_TRANSPORT_OK
           ? MAX35103_OK
           : MAX35103_SPI_ERROR;
}

Max35103Status MAX35103_WriteReg(Max35103Driver *drv,
                                 uint8_t write_opcode, uint16_t value)
{
    if (!drv || !max_is_write_opcode(write_opcode)) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->device_ready) {
        return MAX35103_NOT_READY;
    }
    if (drv->state != MAX35103_STATE_IDLE || drv->event_timing_active ||
        drv->spi_pending) {
        return MAX35103_BUSY;
    }

    return max_spi_write_reg(drv, write_opcode, value) ==
               MAX35103_TRANSPORT_OK
           ? MAX35103_OK
           : MAX35103_SPI_ERROR;
}

Max35103Status MAX35103_WriteVerifyReg(Max35103Driver *drv,
                                       uint8_t write_opcode, uint16_t value)
{
    Max35103Status write_status = MAX35103_WriteReg(drv,
                                                    write_opcode,
                                                    value);
    if (write_status != MAX35103_OK) {
        return write_status;
    }

    uint16_t readback = 0U;
    Max35103Status read_status = MAX35103_ReadReg(
        drv, max_readback_opcode(write_opcode), &readback);
    if (read_status != MAX35103_OK) {
        return read_status;
    }
    return readback == value ? MAX35103_OK : MAX35103_CONFIG_ERROR;
}

/* -------------------------------------------------------------------------- */
/* Result and status access                                                   */
/* -------------------------------------------------------------------------- */

bool MAX35103_HasResult(const Max35103Driver *drv)
{
    return drv && drv->result_pending;
}

Max35103Status MAX35103_GetResult(Max35103Driver *drv,
                                  Max35103RawResult *result)
{
    if (!drv || !result) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->result_pending) {
        return MAX35103_NO_RESULT;
    }

    *result = drv->result;
    drv->result_pending = false;
    drv->result.valid = false;
    return MAX35103_OK;
}

bool MAX35103_HasTemperatureResult(const Max35103Driver *drv)
{
    return drv && drv->temperature_result_pending;
}

Max35103Status MAX35103_GetTemperatureResult(
    Max35103Driver *drv, Max35103TemperatureResult *result)
{
    if (!drv || !result) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->temperature_result_pending) {
        return MAX35103_NO_RESULT;
    }

    *result = drv->temperature_result;
    drv->temperature_result_pending = false;
    drv->temperature_result.valid = false;
    return MAX35103_OK;
}

Max35103Status MAX35103_ReadResult(Max35103Driver *drv,
                                   Max35103RawResult *result)
{
    if (!drv || !result) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->device_ready) {
        return MAX35103_NOT_READY;
    }
    if (drv->spi_pending) {
        return MAX35103_BUSY;
    }

    uint16_t status = 0U;
    if (max_spi_read_reg(drv, MAX35103_REG_INT_STATUS, &status) !=
        MAX35103_TRANSPORT_OK) {
        return MAX35103_SPI_ERROR;
    }
    if (status == 0xFFFFU) {
        return MAX35103_DEVICE_ERROR;
    }

    const uint16_t tof_ready = MAX35103_INT_TOF_COMPLETE |
                               MAX35103_INT_TOF_EVTMG;
    if ((status & tof_ready) == 0U) {
        if ((status & (MAX35103_INT_TIMEOUT | MAX35103_INT_POR)) != 0U) {
            memset(result, 0, sizeof(*result));
            result->status_flags = status;
            return MAX35103_DEVICE_ERROR;
        }
        return MAX35103_NO_RESULT;
    }

    return max_read_tof_words_blocking(drv, status, 0U,
                                       drv->event_timing_active, result);
}

uint8_t MAX35103_ConfiguredHitCount(const Max35103Profile *profile)
{
    if (!profile) {
        return 0U;
    }

    uint8_t hits = (uint8_t)(
        ((profile->tof2 & MAX35103_TOF2_STOP_MASK) >> 13) + 1U);
    if (hits > MAX35103_WAVE_HIT_COUNT) {
        hits = MAX35103_WAVE_HIT_COUNT;
    }
    return hits;
}

Max35103Status MAX35103_ReadWaveEvidence(
    Max35103Driver *drv, Max35103WaveEvidence *evidence)
{
    if (!drv || !evidence) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->device_ready || !drv->configured || !drv->profile) {
        return MAX35103_NOT_READY;
    }
    if (MAX35103_IsBusy(drv) || drv->event_timing_active) {
        return MAX35103_BUSY;
    }

    memset(evidence, 0, sizeof(*evidence));
    evidence->configured_hit_count =
        MAX35103_ConfiguredHitCount(drv->profile);
    if (evidence->configured_hit_count == 0U) {
        return MAX35103_CONFIG_ERROR;
    }

    if (max_spi_read_reg(drv, MAX35103_REG_WVRUP,
                         &evidence->wvr_up) !=
            MAX35103_TRANSPORT_OK ||
        max_spi_read_reg(drv, MAX35103_REG_WVRDN,
                         &evidence->wvr_down) !=
            MAX35103_TRANSPORT_OK) {
        return MAX35103_SPI_ERROR;
    }

    evidence->wvr_up_t1_t2_q7 = (uint8_t)(evidence->wvr_up >> 8);
    evidence->wvr_up_t2_ideal_q7 = (uint8_t)evidence->wvr_up;
    evidence->wvr_down_t1_t2_q7 =
        (uint8_t)(evidence->wvr_down >> 8);
    evidence->wvr_down_t2_ideal_q7 =
        (uint8_t)evidence->wvr_down;

    bool valid = evidence->wvr_up != 0xFFFFU &&
                 evidence->wvr_down != 0xFFFFU &&
                 evidence->wvr_up_t1_t2_q7 != 0U &&
                 evidence->wvr_up_t2_ideal_q7 != 0U &&
                 evidence->wvr_down_t1_t2_q7 != 0U &&
                 evidence->wvr_down_t2_ideal_q7 != 0U;
    uint32_t previous_up = 0U;
    uint32_t previous_down = 0U;

    for (uint8_t hit = 0U;
         hit < evidence->configured_hit_count;
         ++hit) {
        if (max_spi_read_reg(drv, kHitUpIntOpcodes[hit],
                             &evidence->hit_up_int[hit]) !=
                MAX35103_TRANSPORT_OK ||
            max_spi_read_reg(drv, kHitUpFracOpcodes[hit],
                             &evidence->hit_up_frac[hit]) !=
                MAX35103_TRANSPORT_OK ||
            max_spi_read_reg(drv, kHitDownIntOpcodes[hit],
                             &evidence->hit_down_int[hit]) !=
                MAX35103_TRANSPORT_OK ||
            max_spi_read_reg(drv, kHitDownFracOpcodes[hit],
                             &evidence->hit_down_frac[hit]) !=
                MAX35103_TRANSPORT_OK) {
            return MAX35103_SPI_ERROR;
        }

        evidence->hit_up_q16[hit] =
            ((uint32_t)evidence->hit_up_int[hit] << 16) |
            evidence->hit_up_frac[hit];
        evidence->hit_down_q16[hit] =
            ((uint32_t)evidence->hit_down_int[hit] << 16) |
            evidence->hit_down_frac[hit];
        evidence->hit_up_ps[hit] =
            max_q16_unsigned_to_ps(evidence->hit_up_q16[hit]);
        evidence->hit_down_ps[hit] =
            max_q16_unsigned_to_ps(evidence->hit_down_q16[hit]);

        if ((evidence->hit_up_int[hit] & 0x8000U) != 0U ||
            (evidence->hit_down_int[hit] & 0x8000U) != 0U ||
            evidence->hit_up_q16[hit] == 0U ||
            evidence->hit_down_q16[hit] == 0U ||
            evidence->hit_up_q16[hit] == UINT32_C(0xFFFFFFFF) ||
            evidence->hit_down_q16[hit] == UINT32_C(0xFFFFFFFF) ||
            (hit != 0U &&
             (evidence->hit_up_q16[hit] <= previous_up ||
              evidence->hit_down_q16[hit] <= previous_down))) {
            valid = false;
        }

        previous_up = evidence->hit_up_q16[hit];
        previous_down = evidence->hit_down_q16[hit];
    }

    evidence->valid = valid;
    return valid ? MAX35103_OK : MAX35103_DEVICE_ERROR;
}

bool MAX35103_IsBusy(const Max35103Driver *drv)
{
    if (!drv) {
        return false;
    }

    return drv->spi_pending ||
           drv->state == MAX35103_STATE_ARMING ||
           drv->state == MAX35103_STATE_DRAIN_STATUS ||
           drv->state == MAX35103_STATE_READ_RESULT ||
           drv->state == MAX35103_STATE_READ_TEMP_RESULT ||
           drv->state == MAX35103_STATE_HALTING ||
           drv->state == MAX35103_STATE_SELF_CHECK ||
           drv->state == MAX35103_STATE_TEMP_MEASURING;
}

Max35103State MAX35103_GetState(const Max35103Driver *drv)
{
    return drv ? drv->state : MAX35103_STATE_UNINIT;
}

bool MAX35103_Probe(Max35103Driver *drv)
{
    if (!drv || !drv->device_ready || MAX35103_IsBusy(drv)) {
        return false;
    }

    uint16_t tof1 = 0U;
    uint16_t control = 0U;
    if (max_spi_read_reg(drv, MAX35103_READ_TOF1, &tof1) !=
            MAX35103_TRANSPORT_OK ||
        max_spi_read_reg(drv, MAX35103_READ_CAL_CTRL, &control) !=
            MAX35103_TRANSPORT_OK) {
        return false;
    }

    /* Reject common disconnected-bus values and impossible reserved bits. */
    if (tof1 == 0x0000U || tof1 == 0xFFFFU ||
        control == 0xFFFFU ||
        (tof1 & 0x0004U) != 0U ||
        (control & 0xF000U) != 0U) {
        return false;
    }
    return true;
}
