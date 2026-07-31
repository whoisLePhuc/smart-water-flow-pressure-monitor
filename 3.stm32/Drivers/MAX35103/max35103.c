/**
  ******************************************************************************
  * @file    max35103.c
  * @brief   Portable MAX35103 time-of-flight and temperature driver
  *
  * @details
  * This file contains no MCU-specific register or HAL access. All physical
  * transport, reset, time, delay, and optional bus-lock operations enter
  * through Max35103Transport.
  *
  * Deferred event processing follows this ownership sequence:
  *
  * @code
  * EVENT_RUNNING
  *   --MAX_INT--> DRAIN_STATUS
  *   --TOF flag--> READ_RESULT ---------+
  *   --TEMP flag-> READ_TEMP_RESULT ----+--> EVENT_RUNNING or IDLE
  * @endcode
  *
  * INT_STATUS is self-clearing on read, so its snapshot is latched exactly
  * once and carried through the result decode. EVTMG1 may require two result
  * reads; both are published with one host sequence number. SPI callbacks use
  * nonzero tokens to reject stale DMA completion after cancellation or reset.
  *
  * Public API contracts live in max35103.h. Implementation comments here focus
  * on protocol constraints, state ownership, validation gates, and recovery.
  ******************************************************************************
  */

#include "max35103.h"

#include <string.h>

/* A nominal 4 MHz reference period is 250 ns = 250000 ps. */
#define MAX35103_NOMINAL_CLOCK_PERIOD_PS  INT64_C(250000)
/* Denominator of the device's integer-plus-16-bit-fraction time format. */
#define MAX35103_Q16_SCALE                INT64_C(65536)
/*
 * Coherence tolerance in Q16 LSBs (~3.8 ps each). Real silicon has a natural
 * rounding gap of ~11 LSB between the direct and averaged DIFF registers, so
 * the historic tolerance of 1 LSB rejected every real-world measurement
 * (observed: healthy result failed by exactly 1 LSB). 64 LSB (~244 ps) is an
 * integrity bound that catches bank/corruption errors (thousands of LSB)
 * while passing healthy hardware data.
 */
#define MAX35103_COHERENCE_TOLERANCE_Q16  INT64_C(64)
#define MAX35103_TOF_BANK_WORD_INDEX(opcode) \
    ((uint8_t)((opcode) - MAX35103_REG_WVRUP))

typedef struct {
    /** MAX35103 configuration write opcode. */
    uint8_t write_opcode;
    /** Complete 16-bit value written and then compared by readback. */
    uint16_t value;
} Max35103ConfigEntry;

static bool max_schedule_register_read(Max35103Driver *drv,
                                       uint8_t read_opcode);
static void max_publish_status_only(Max35103Driver *drv,
                                    uint16_t status,
                                    uint64_t timestamp_us);
static void max_publish_temperature_status_only(
    Max35103Driver *drv, uint16_t status, uint64_t timestamp_us);

/* -------------------------------------------------------------------------- */
/* Injected platform transport                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Validate required hooks and optional-hook pairing.
 *
 * Lock/unlock and async start/cancel are each atomic capability pairs. Accepting
 * only one hook would make recovery unable to release the resource it acquired.
 */
static bool max_transport_valid(const Max35103Transport *transport)
{
    return transport != NULL &&
           transport->transfer != NULL &&
           transport->get_tick_ms != NULL &&
           transport->delay_ms != NULL &&
           ((transport->lock == NULL && transport->unlock == NULL) ||
            (transport->lock != NULL && transport->unlock != NULL)) &&
           ((transport->start_transfer_async == NULL &&
             transport->cancel_transfer_async == NULL) ||
            (transport->start_transfer_async != NULL &&
             transport->cancel_transfer_async != NULL));
}

/**
 * @brief Acquire logical and optional platform ownership of the SPI bus.
 *
 * spi_bus_locked protects against reentry even on a dedicated bus. When a
 * platform lock is installed, the logical flag is set only after that lock
 * succeeds so a failed acquisition never requires an unlock.
 */
static Max35103TransportStatus max_bus_acquire(Max35103Driver *drv)
{
    if (!drv || drv->spi_bus_locked) {
        return MAX35103_TRANSPORT_BUSY;
    }

    if (drv->transport.lock != NULL) {
        const Max35103TransportStatus status = drv->transport.lock(
            drv->transport.context, MAX35103_SPI_TIMEOUT_MS);
        if (status != MAX35103_TRANSPORT_OK) {
            drv->bus_lock_failure_count++;
            return status;
        }
    }

    drv->spi_bus_locked = true;
    return MAX35103_TRANSPORT_OK;
}

/**
 * @brief Release exactly one bus acquisition.
 *
 * Clear the logical flag before invoking application code so an unlock hook
 * cannot observe the driver as still owning the bus.
 */
static void max_bus_release(Max35103Driver *drv)
{
    if (!drv || !drv->spi_bus_locked) {
        return;
    }

    drv->spi_bus_locked = false;
    if (drv->transport.unlock != NULL) {
        drv->transport.unlock(drv->transport.context);
    }
}

/**
 * @brief Execute one complete blocking frame under shared-bus ownership.
 *
 * The platform transfer hook owns NSS timing for the whole frame. The core
 * owns only the higher-level bus mutex and always releases it after the hook
 * returns, regardless of transport status.
 */
static Max35103TransportStatus max_spi_xfer(
    Max35103Driver *drv, const uint8_t *tx, uint8_t *rx, uint16_t length)
{
    if (drv == NULL || !max_transport_valid(&drv->transport) ||
        tx == NULL || length == 0U) {
        return MAX35103_TRANSPORT_ERROR;
    }

    Max35103TransportStatus status = max_bus_acquire(drv);
    if (status != MAX35103_TRANSPORT_OK) {
        return status;
    }

    status = drv->transport.transfer(
        drv->transport.context, tx, rx, length, MAX35103_SPI_TIMEOUT_MS);
    max_bus_release(drv);
    return status;
}

/** @brief Send one execution opcode with no returned data. */
static Max35103TransportStatus max_spi_command(
    Max35103Driver *drv, uint8_t opcode)
{
    return max_spi_xfer(drv, &opcode, NULL, 1U);
}

/**
 * @brief Read one big-endian 16-bit register in a three-byte SPI frame.
 *
 * rx[0] is the byte received while the opcode is transmitted and is discarded;
 * the register value is returned in rx[1] and rx[2].
 */
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

/**
 * @brief Prove that a sequential read stays inside the supported register map.
 *
 * CONTROL is a special one-word read at 0x7F. Normal sequential reads start in
 * the 0xB0..0xFE read-opcode space and may end at, but never wrap beyond,
 * INT_STATUS.
 */
static bool max_block_range_valid(uint8_t start_read_opcode,
                                  uint8_t word_count)
{
    if (word_count == 0U || word_count > MAX35103_MAX_BLOCK_WORDS) {
        return false;
    }
    if (start_read_opcode == MAX35103_REG_CONTROL) {
        return word_count == 1U;
    }
    if (start_read_opcode < 0xB0U ||
        start_read_opcode > MAX35103_REG_INT_STATUS) {
        return false;
    }

    const uint16_t end_opcode = (uint16_t)(
        (uint16_t)start_read_opcode + (uint16_t)word_count - 1U);
    return end_opcode <= MAX35103_REG_INT_STATUS;
}

/**
 * @brief Read consecutive register bytes with one opcode and one NSS interval.
 *
 * The result bank must be read as one frame. Reissuing an opcode per word or
 * raising NSS between words would reset the device's serial address sequence.
 */
static Max35103TransportStatus max_spi_read_block_data(
    Max35103Driver *drv, uint8_t start_read_opcode,
    uint8_t *data, uint8_t word_count)
{
    if (!drv || !data ||
        !max_block_range_valid(start_read_opcode, word_count)) {
        return MAX35103_TRANSPORT_ERROR;
    }

    const uint16_t data_length = (uint16_t)word_count * 2U;
    const uint16_t frame_length = data_length + 1U;
    memset(drv->tx_buf, 0, frame_length);
    memset(drv->rx_buf, 0, frame_length);
    drv->tx_buf[0] = start_read_opcode;

    const Max35103TransportStatus transport_status = max_spi_xfer(
        drv, drv->tx_buf, drv->rx_buf, frame_length);
    if (transport_status == MAX35103_TRANSPORT_OK) {
        memcpy(data, &drv->rx_buf[1], data_length);
    }
    return transport_status;
}

/** @brief Write one big-endian 16-bit register in a three-byte SPI frame. */
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

/** @brief Return the read opcode corresponding to a configuration write. */
static uint8_t max_readback_opcode(uint8_t write_opcode)
{
    return write_opcode == 0xFFU
           ? MAX35103_REG_CONTROL
           : (uint8_t)(write_opcode | 0x80U);
}

static bool max_is_configuration_write(uint8_t write_opcode)
{
    return write_opcode >= 0x30U && write_opcode <= 0x43U;
}

/**
 * @brief Apply a verified individual register value to a profile shadow.
 *
 * false means the opcode is not represented by Max35103Profile and therefore
 * cannot preserve proof that the complete active register image is known.
 */
static bool max_update_profile_register(Max35103Profile *profile,
                                        uint8_t write_opcode,
                                        uint16_t value)
{
    if (!profile) {
        return false;
    }

    switch (write_opcode) {
    case MAX35103_REG_TOF1:
        profile->tof1 = value;
        return true;
    case MAX35103_REG_TOF2:
        profile->tof2 = value;
        return true;
    case MAX35103_REG_TOF3:
        profile->tof3 = value;
        return true;
    case MAX35103_REG_TOF4:
        profile->tof4 = value;
        return true;
    case MAX35103_REG_TOF5:
        profile->tof5 = value;
        return true;
    case MAX35103_REG_TOF6:
        profile->tof6 = value;
        return true;
    case MAX35103_REG_TOF7:
        profile->tof7 = value;
        return true;
    case MAX35103_REG_EVT_TIMING_1:
        profile->event_timing_1 = value;
        return true;
    case MAX35103_REG_EVT_TIMING_2:
        profile->event_timing_2 = value;
        return true;
    case MAX35103_REG_TOF_MEAS_DELAY:
        profile->tof_measurement_delay = value;
        return true;
    case MAX35103_REG_CAL_CTRL:
        profile->calibration_control = value;
        return true;
    default:
        return false;
    }
}

static bool max_direct_pl_stop_valid(uint8_t write_opcode,
                                     uint16_t value)
{
    /*
     * Direct register APIs cannot validate an entire unknown profile, but PL
     * and STOP have independent safety bounds that must never be bypassed.
     */
    if (write_opcode == MAX35103_REG_TOF1) {
        const uint8_t pl = (uint8_t)(
            (value & MAX35103_TOF1_PL_MASK) >>
            MAX35103_TOF1_PL_SHIFT);
        return pl >= MAX35103_PL_MIN && pl <= MAX35103_PL_MAX;
    }
    if (write_opcode == MAX35103_REG_TOF2) {
        const uint8_t stop_code = (uint8_t)(
            (value & MAX35103_TOF2_STOP_MASK) >>
            MAX35103_TOF2_STOP_SHIFT);
        return stop_code <= MAX35103_STOP_CODE_MAX;
    }
    return true;
}

static bool max_profile_synchronized(const Max35103Driver *drv)
{
    return drv && drv->configured && drv->profile_synchronized;
}

static const Max35103Profile *max_active_profile(
    const Max35103Driver *drv)
{
    return max_profile_synchronized(drv) ? &drv->active_profile : NULL;
}

static void max_invalidate_profile(Max35103Driver *drv)
{
    if (!drv) {
        return;
    }
    drv->configured = false;
    drv->profile_synchronized = false;
}

/* -------------------------------------------------------------------------- */
/* State, timeout, and result-queue helpers                                   */
/* -------------------------------------------------------------------------- */

/** @brief Resolve a zero profile timeout to its compile-time default. */
static uint32_t max_profile_timeout(uint32_t configured,
                                    uint32_t fallback)
{
    return configured != 0U ? configured : fallback;
}

static uint32_t max_init_timeout(const Max35103Driver *drv)
{
    const Max35103Profile *profile = max_active_profile(drv);
    return profile
           ? max_profile_timeout(profile->init_timeout_ms,
                                 MAX35103_INIT_TIMEOUT_MS)
           : MAX35103_INIT_TIMEOUT_MS;
}

static uint32_t max_result_timeout(const Max35103Driver *drv)
{
    const Max35103Profile *profile = max_active_profile(drv);
    return profile
           ? max_profile_timeout(profile->result_timeout_ms,
                                 MAX35103_RESULT_TIMEOUT_MS)
           : MAX35103_RESULT_TIMEOUT_MS;
}

static uint32_t max_halt_timeout(const Max35103Driver *drv)
{
    const Max35103Profile *profile = max_active_profile(drv);
    return profile
           ? max_profile_timeout(profile->halt_timeout_ms,
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

/**
 * @brief Test a wrapping 32-bit millisecond tick without absolute-time compare.
 *
 * Unsigned subtraction remains correct across HAL_GetTick() wraparound as long
 * as timeout_ms is less than half the counter range, which is true here.
 */
static bool max_tick_expired(const Max35103Driver *drv,
                             uint32_t start_ms, uint32_t timeout_ms)
{
    return (uint32_t)(max_get_tick_ms(drv) - start_ms) >= timeout_ms;
}

/** @brief Clear request metadata after completion or cancellation. */
static void max_clear_pending_spi(Max35103Driver *drv)
{
    drv->spi_pending = false;
    drv->spi_length = 0U;
    drv->spi_token = 0U;
}

/**
 * @brief Cancel async ownership, release the bus, and clear request metadata.
 *
 * The platform cancel hook is called only for a transfer already marked active.
 * Regardless of platform abort success, host ownership is cleared so recovery
 * cannot deadlock the core. Counters preserve whether the platform cooperated.
 */
static void max_cancel_pending_spi(Max35103Driver *drv)
{
    if (!drv) {
        return;
    }

    if (drv->spi_async_active) {
        if (drv->transport.cancel_transfer_async != NULL) {
            const Max35103TransportStatus status =
                drv->transport.cancel_transfer_async(
                    drv->transport.context, drv->spi_token);
            if (status == MAX35103_TRANSPORT_OK) {
                drv->dma_cancel_count++;
            } else {
                drv->dma_error_count++;
            }
        }
        drv->spi_async_active = false;
        max_bus_release(drv);
    }
    max_clear_pending_spi(drv);
}

/**
 * @brief Reset transient per-operation fields while preserving configuration.
 *
 * Queue contents and lifetime counters are intentionally not cleared here.
 */
static void max_clear_operation(Max35103Driver *drv)
{
    drv->attempt_start_us = 0U;
    drv->deadline_us = 0U;
    drv->result_word_index = 0U;
    drv->temperature_cycle_word = 0U;
    drv->latched_status = 0U;
    drv->interrupt_timestamp_us = 0U;
    drv->pending_irq_timestamp_us = 0U;
    drv->irq_recheck_pending = false;
    drv->active_event_sequence_number = 0U;
    drv->active_event_sequence_valid = false;
    max_cancel_pending_spi(drv);
}

/**
 * @brief Enter the terminal operation-error state with full resource cleanup.
 *
 * generation is incremented so diagnostics can distinguish recovery epochs.
 */
static void max_enter_error(Max35103Driver *drv)
{
    drv->error_count++;
    drv->state = MAX35103_STATE_ERROR;
    drv->generation++;
    max_cancel_pending_spi(drv);
    drv->deadline_us = 0U;
    drv->irq_recheck_pending = false;
    drv->pending_irq_timestamp_us = 0U;
    drv->active_event_sequence_number = 0U;
    drv->active_event_sequence_valid = false;
}

static bool max_event_continuous(const Max35103Driver *drv)
{
    const Max35103Profile *profile = max_active_profile(drv);
    return profile &&
           (profile->calibration_control &
           MAX35103_CAL_CTRL_ET_CONT) != 0U;
}

/**
 * @brief Decide whether the FSM exclusively owns self-clearing INT_STATUS.
 *
 * Public diagnostic reads are blocked in these states to prevent them from
 * consuming completion evidence before the FSM can publish a result.
 */
static bool max_int_status_owned(const Max35103Driver *drv)
{
    if (!drv) {
        return false;
    }

    return drv->event_timing_active ||
           drv->state == MAX35103_STATE_ARMING ||
           drv->state == MAX35103_STATE_DRAIN_STATUS ||
           drv->state == MAX35103_STATE_READ_RESULT ||
           drv->state == MAX35103_STATE_READ_TEMP_RESULT ||
           drv->state == MAX35103_STATE_HALTING ||
           drv->state == MAX35103_STATE_SELF_CHECK ||
           drv->state == MAX35103_STATE_TEMP_MEASURING;
}

/**
 * @brief Map the configured event command to required completion flags.
 *
 * EVTMG1 publishes both TOF and temperature; EVTMG2 publishes TOF only; EVTMG3
 * publishes temperature only. Completion is declared only after every required
 * flag for the configured mode has been seen.
 */
static uint16_t max_expected_event_flags(const Max35103Driver *drv)
{
    const Max35103Profile *profile = max_active_profile(drv);
    if (!profile) {
        return 0U;
    }

    switch (profile->event_mode_cmd) {
    case MAX35103_CMD_EVTMG1:
        return MAX35103_INT_TOF_EVTMG | MAX35103_INT_TEMP_EVTMG;
    case MAX35103_CMD_EVTMG2:
        return MAX35103_INT_TOF_EVTMG;
    case MAX35103_CMD_EVTMG3:
        return MAX35103_INT_TEMP_EVTMG;
    default:
        return 0U;
    }
}

/** @brief Decode EVT_TIMING_2 TP bits into T1..T4 result-port masks. */
static uint8_t max_selected_temperature_ports(const Max35103Driver *drv)
{
    const Max35103Profile *profile = max_active_profile(drv);
    if (!profile) {
        return 0U;
    }

    switch (profile->event_timing_2 &
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

/**
 * @brief Enter status-drain state and schedule the self-clearing register read.
 *
 * The first edge opens one event deadline. A recheck edge while the FSM is busy
 * reuses that deadline instead of extending a failing event indefinitely.
 */
static bool max_begin_status_drain(Max35103Driver *drv, uint64_t now_us)
{
    if (!drv) {
        return false;
    }

    drv->state = MAX35103_STATE_DRAIN_STATUS;
    drv->attempt_start_us = now_us;
    drv->interrupt_timestamp_us = now_us;
    if (drv->deadline_us == 0U) {
        drv->deadline_us =
            now_us + (uint64_t)max_result_timeout(drv) * UINT64_C(1000);
    }
    drv->latched_status = 0U;
    drv->result_word_index = 0U;
    return max_schedule_register_read(drv, MAX35103_REG_INT_STATUS);
}

/**
 * @brief Publish invalid placeholders for expected event parts never received.
 *
 * Status-only records keep the event sequence and queue/counter accounting
 * observable instead of silently losing one half of an EVTMG1 cycle.
 */
static void max_publish_missing_event_results(Max35103Driver *drv,
                                              uint16_t status)
{
    const uint16_t missing = (uint16_t)(
        drv->expected_event_flags & (uint16_t)~drv->seen_event_flags);

    if ((missing & MAX35103_INT_TOF_EVTMG) != 0U) {
        max_publish_status_only(
            drv, status, drv->interrupt_timestamp_us);
    }
    if ((missing & MAX35103_INT_TEMP_EVTMG) != 0U) {
        max_publish_temperature_status_only(
            drv, status, drv->interrupt_timestamp_us);
    }
}

/** @brief Enter event timeout after publishing all missing expected records. */
static void max_enter_event_timeout(Max35103Driver *drv, uint16_t status)
{
    max_publish_missing_event_results(drv, status);
    drv->timeout_count++;
    drv->error_count++;
    drv->generation++;
    max_cancel_pending_spi(drv);
    drv->deadline_us = 0U;
    drv->irq_recheck_pending = false;
    drv->pending_irq_timestamp_us = 0U;
    if (!max_event_continuous(drv)) {
        drv->event_timing_active = false;
    }
    drv->active_event_sequence_number = 0U;
    drv->active_event_sequence_valid = false;
    drv->state = MAX35103_STATE_TIMEOUT;
}

/**
 * @brief Close one INT_STATUS/result-drain pass and choose the next FSM state.
 *
 * A MAX_INT edge observed during an SPI/result operation is represented by
 * irq_recheck_pending and immediately starts another status drain. Otherwise,
 * continuous event timing returns to EVENT_RUNNING and one-shot timing returns
 * to IDLE once all expected flags have been published.
 */
static void max_finish_event_interrupt(Max35103Driver *drv,
                                       uint16_t status)
{
    if ((status & MAX35103_INT_TIMEOUT) != 0U) {
        max_enter_event_timeout(drv, status);
        return;
    }

    const bool event_complete =
        drv->expected_event_flags != 0U &&
        (drv->seen_event_flags & drv->expected_event_flags) ==
            drv->expected_event_flags;

    if (event_complete) {
        if (!max_event_continuous(drv)) {
            drv->event_timing_active = false;
        }
        drv->seen_event_flags = 0U;
        drv->deadline_us = 0U;
        drv->active_event_sequence_number = 0U;
        drv->active_event_sequence_valid = false;
    }

    drv->attempt_start_us = 0U;
    drv->result_word_index = 0U;
    drv->temperature_cycle_word = 0U;

    if (drv->irq_recheck_pending) {
        const uint64_t pending_timestamp =
            drv->pending_irq_timestamp_us != 0U
            ? drv->pending_irq_timestamp_us
            : drv->interrupt_timestamp_us;
        drv->irq_recheck_pending = false;
        drv->pending_irq_timestamp_us = 0U;
        if (!max_begin_status_drain(drv, pending_timestamp)) {
            max_enter_error(drv);
        }
        return;
    }

    if (!event_complete && drv->seen_event_flags == 0U) {
        drv->deadline_us = 0U;
    }
    drv->state = drv->event_timing_active
                 ? MAX35103_STATE_EVENT_RUNNING
                 : MAX35103_STATE_IDLE;
}

/**
 * @brief Stage one register read for blocking or async execution.
 *
 * Tokens skip zero because zero means "no active request". This makes cleared
 * state distinguishable from every real completion, including after wraparound.
 */
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

/** @brief Stage one bounded sequential read with a single nonzero token. */
static bool max_schedule_block_read(Max35103Driver *drv,
                                    uint8_t start_read_opcode,
                                    uint8_t word_count)
{
    if (!drv || drv->spi_pending ||
        !max_block_range_valid(start_read_opcode, word_count)) {
        return false;
    }

    drv->spi_length = (uint16_t)(
        1U + (uint16_t)word_count * 2U);
    memset(drv->tx_buf, 0, drv->spi_length);
    memset(drv->rx_buf, 0, drv->spi_length);
    drv->tx_buf[0] = start_read_opcode;

    drv->next_spi_token++;
    if (drv->next_spi_token == 0U) {
        drv->next_spi_token++;
    }
    drv->spi_token = drv->next_spi_token;
    drv->spi_pending = true;
    return true;
}

/**
 * @brief Stage direct or event-averaged temperature result-bank reading.
 *
 * Averaged layout starts with TEMP_CYCLE_COUNT followed by T1_AVG..T4_AVG;
 * direct layout contains only T1..T4 integer/fraction pairs.
 */
static bool max_begin_temperature_read(Max35103Driver *drv, bool averaged)
{
    drv->state = MAX35103_STATE_READ_TEMP_RESULT;
    drv->result_word_index = 0U;
    drv->temperature_cycle_word = 0U;
    memset(drv->temperature_frame, 0, sizeof(drv->temperature_frame));
    return max_schedule_block_read(
        drv,
        averaged ? MAX35103_REG_TEMP_CYCLE_COUNT : MAX35103_REG_T1_INT,
        averaged ? MAX35103_TEMP_AVG_BLOCK_WORDS
                 : MAX35103_TEMP_RESULT_WORDS);
}

/** @brief Allocate a nonzero host sequence number, skipping zero on wrap. */
static uint32_t max_allocate_sequence(Max35103Driver *drv)
{
    drv->next_sequence_number++;
    if (drv->next_sequence_number == 0U) {
        drv->next_sequence_number++;
    }
    return drv->next_sequence_number;
}

/**
 * @brief Return the sequence number used for the next published record.
 *
 * Direct operations receive a fresh number. Every result from one active event
 * shares a lazily allocated number, pairing TOF and temperature in EVTMG1.
 */
static uint32_t max_publish_sequence(Max35103Driver *drv)
{
    if (!drv->event_timing_active) {
        return max_allocate_sequence(drv);
    }

    if (!drv->active_event_sequence_valid) {
        drv->active_event_sequence_number = max_allocate_sequence(drv);
        drv->active_event_sequence_valid = true;
    }
    return drv->active_event_sequence_number;
}

/**
 * @brief Push one TOF result according to the selected overflow policy.
 *
 * Overflow and dropped counters increment for both policies. DROP_OLDEST moves
 * the head before appending; DROP_NEWEST preserves the existing FIFO and exits.
 */
static void max_publish_result(Max35103Driver *drv,
                               const Max35103RawResult *result)
{
    if (!drv || !result) {
        return;
    }

    Max35103RawResult queued = *result;
    if (queued.sequence_number == 0U) {
        queued.sequence_number = max_publish_sequence(drv);
    }

    if (drv->result_queue_count >= MAX35103_RESULT_QUEUE_CAPACITY) {
        drv->result_queue_overflow_count++;
        drv->dropped_result_count++;
        if (drv->queue_overflow_policy == MAX35103_QUEUE_DROP_NEWEST) {
            return;
        }
        drv->result_queue_head = (uint8_t)(
            ((uint16_t)drv->result_queue_head + 1U) %
            MAX35103_RESULT_QUEUE_CAPACITY);
        drv->result_queue_count--;
    }

    const uint8_t tail = (uint8_t)(
        ((uint16_t)drv->result_queue_head +
        (uint16_t)drv->result_queue_count) %
        MAX35103_RESULT_QUEUE_CAPACITY);
    drv->result_queue[tail] = queued;
    drv->result_queue_count++;

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

/**
 * @brief Push one temperature result using the same FIFO policy as TOF.
 */
static void max_publish_temperature_result(
    Max35103Driver *drv, const Max35103TemperatureResult *result)
{
    if (!drv || !result) {
        return;
    }

    Max35103TemperatureResult queued = *result;
    if (queued.sequence_number == 0U) {
        queued.sequence_number = max_publish_sequence(drv);
    }

    if (drv->temperature_queue_count >=
        MAX35103_TEMPERATURE_QUEUE_CAPACITY) {
        drv->temperature_queue_overflow_count++;
        drv->dropped_temperature_result_count++;
        if (drv->queue_overflow_policy == MAX35103_QUEUE_DROP_NEWEST) {
            return;
        }
        drv->temperature_queue_head = (uint8_t)(
            ((uint16_t)drv->temperature_queue_head + 1U) %
            MAX35103_TEMPERATURE_QUEUE_CAPACITY);
        drv->temperature_queue_count--;
    }

    const uint8_t tail = (uint8_t)(
        ((uint16_t)drv->temperature_queue_head +
        (uint16_t)drv->temperature_queue_count) %
        MAX35103_TEMPERATURE_QUEUE_CAPACITY);
    drv->temperature_queue[tail] = queued;
    drv->temperature_queue_count++;

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

/**
 * @brief Convert unsigned Q16 reference-clock periods to nominal picoseconds.
 *
 * Integer arithmetic deliberately truncates sub-picosecond remainder. The
 * conversion assumes 4 MHz and does not include oscillator calibration.
 */
static int64_t max_q16_unsigned_to_ps(uint32_t value)
{
    return ((int64_t)value * MAX35103_NOMINAL_CLOCK_PERIOD_PS) /
           MAX35103_Q16_SCALE;
}

/**
 * @brief Convert signed Q16 reference-clock periods to nominal picoseconds.
 *
 * Magnitude is converted before sign restoration so negative integer division
 * has the same truncation behavior as the positive path.
 */
static int64_t max_q16_signed_to_ps(int64_t value)
{
    if (value >= 0) {
        return (value * MAX35103_NOMINAL_CLOCK_PERIOD_PS) /
               MAX35103_Q16_SCALE;
    }

    return -(((-value) * MAX35103_NOMINAL_CLOCK_PERIOD_PS) /
             MAX35103_Q16_SCALE);
}

/**
 * @brief Extract one big-endian 16-bit word from the sequential TOF bank.
 *
 * The bank begins at WVRUP; subtracting its opcode converts an absolute device
 * opcode into a zero-based word index.
 */
static uint16_t max_tof_bank_word(const uint8_t *bank,
                                  uint8_t read_opcode)
{
    const uint16_t byte_index =
        (uint16_t)MAX35103_TOF_BANK_WORD_INDEX(read_opcode) * 2U;
    return (uint16_t)(((uint16_t)bank[byte_index] << 8) |
                      (uint16_t)bank[byte_index + 1U]);
}

/**
 * @brief Reconstruct a signed integer-plus-fraction register pair as Q16.
 *
 * The integer word is interpreted as int16_t while the fractional word remains
 * unsigned. For example, integer -1 and fraction 0x8000 represent -0.5 periods.
 */
static int32_t max_signed_q16(uint16_t integer, uint16_t fraction)
{
    const int64_t value = (int64_t)(int16_t)integer *
                          MAX35103_Q16_SCALE + fraction;
    return (int32_t)value;
}

/**
 * @brief Decode and validate one complete TOF result-bank snapshot.
 *
 * Validation gates are intentionally ordered from inexpensive status/sentinel
 * checks to derived coherence:
 * 1. Reject TIMEOUT and POR status.
 * 2. Reject negative AVG integer fields and device sentinel values.
 * 3. Require at least one valid cycle for hardware-averaged EVTMG data.
 * 4. Compare the selected difference against AVGUP - AVGDN within the bounded
 *    Q16 tolerance used to detect torn/corrupted banks.
 *
 * The structure is always populated with raw/derived evidence before false is
 * returned, allowing callers to diagnose which values failed validation.
 */
static bool max_decode_tof_bank(const uint8_t *bank,
                                uint16_t status,
                                uint64_t timestamp_us,
                                bool use_average,
                                Max35103RawResult *result)
{
    if (!bank || !result) {
        return false;
    }

    memset(result, 0, sizeof(*result));

    result->avg_up_int =
        max_tof_bank_word(bank, MAX35103_REG_AVGUP_INT);
    result->avg_up_frac =
        max_tof_bank_word(bank, MAX35103_REG_AVGUP_FRAC);
    result->avg_down_int =
        max_tof_bank_word(bank, MAX35103_REG_AVGDN_INT);
    result->avg_down_frac =
        max_tof_bank_word(bank, MAX35103_REG_AVGDN_FRAC);
    result->tof_diff_int =
        max_tof_bank_word(bank, MAX35103_REG_TOF_DIFF_INT);
    result->tof_diff_frac =
        max_tof_bank_word(bank, MAX35103_REG_TOF_DIFF_FRAC);
    result->cycle_range_word =
        max_tof_bank_word(bank, MAX35103_REG_CYCLE_COUNT);
    result->tof_diff_avg_int =
        max_tof_bank_word(bank, MAX35103_REG_TOF_DIFF_AVG_INT);
    result->tof_diff_avg_frac =
        max_tof_bank_word(bank, MAX35103_REG_TOF_DIFF_AVG_FRAC);

    result->tof_range = (uint8_t)(result->cycle_range_word >> 8);
    result->valid_cycle_count = (uint8_t)(result->cycle_range_word & 0xFFU);
    result->status_flags = status;
    result->timestamp_us = timestamp_us;
    result->valid = false;

    result->tof_up_q16 = ((uint32_t)result->avg_up_int << 16) |
                         result->avg_up_frac;
    result->tof_down_q16 = ((uint32_t)result->avg_down_int << 16) |
                           result->avg_down_frac;

    result->tof_diff_q16 = max_signed_q16(
        result->tof_diff_int, result->tof_diff_frac);
    result->tof_diff_avg_q16 = max_signed_q16(
        result->tof_diff_avg_int, result->tof_diff_avg_frac);
    result->selected_tof_diff_is_average = use_average;
    result->selected_tof_diff_q16 = use_average
        ? result->tof_diff_avg_q16
        : result->tof_diff_q16;

    result->tof_up_ps = max_q16_unsigned_to_ps(result->tof_up_q16);
    result->tof_down_ps = max_q16_unsigned_to_ps(result->tof_down_q16);
    result->tof_diff_ps =
        max_q16_signed_to_ps(result->tof_diff_q16);
    result->tof_diff_avg_ps =
        max_q16_signed_to_ps(result->tof_diff_avg_q16);
    result->selected_tof_diff_ps = use_average
        ? result->tof_diff_avg_ps
        : result->tof_diff_ps;

    /* A reset or device timeout invalidates every value in this snapshot. */
    if ((status & (MAX35103_INT_TIMEOUT | MAX35103_INT_POR)) != 0U) {
        return false;
    }

    /* AVG integer fields are 15-bit positive values. */
    if ((result->avg_up_int & 0x8000U) != 0U ||
        (result->avg_down_int & 0x8000U) != 0U) {
        return false;
    }

    /* Reject documented/open-bus all-ones patterns and signed DIFF sentinel. */
    if (result->tof_up_q16 == UINT32_C(0xFFFFFFFF) ||
        result->tof_down_q16 == UINT32_C(0xFFFFFFFF) ||
        (!use_average &&
         result->tof_diff_int == 0x7FFFU &&
         result->tof_diff_frac == 0xFFFFU) ||
        (use_average &&
         result->tof_diff_avg_int == 0x7FFFU &&
         result->tof_diff_avg_frac == 0xFFFFU)) {
        return false;
    }

    if (use_average && result->valid_cycle_count == 0U) {
        return false;
    }

    /*
     * Promote to int64_t before subtraction so unsigned wrap cannot convert a
     * physically negative differential result into a large positive value.
     */
    int64_t expected = (int64_t)result->tof_up_q16 -
                       (int64_t)result->tof_down_q16;
    int64_t coherence_error =
        (int64_t)result->selected_tof_diff_q16 - expected;
    if (coherence_error < 0) {
        coherence_error = -coherence_error;
    }
    if (coherence_error > MAX35103_COHERENCE_TOLERANCE_Q16) {
        return false;
    }

    result->valid = true;
    return true;
}

/**
 * @brief Evaluate IEC 60751 resistance for one temperature and nominal R0.
 *
 * The Callendar-Van Dusen C coefficient applies only below 0 C.
 */
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

    /*
     * IEC 60751 resistance is monotonic over -200..850 C, so integer binary
     * search converges to the two adjacent 1 mC candidates without requiring
     * a floating-point inverse polynomial.
     */
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

    /* Select the adjacent millidegree whose forward resistance is closest. */
    const double low_error = measured - max_platinum_resistance_milliohm(
        low, r0_milliohm);
    const double high_error = max_platinum_resistance_milliohm(
        high, r0_milliohm) - measured;
    *temperature_millicelsius = low_error <= high_error ? low : high;
    return MAX35103_OK;
}

/**
 * @brief Convert a Q16 sensor/reference timing ratio into milliohms.
 *
 * Q16 scale cancels in sensor_q16/reference_q16. Adding half the denominator
 * implements nearest-integer rounding; saturation protects the uint32_t API.
 */
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

/**
 * @brief Decode T1..T4 timing and optional RTD resistance/temperature.
 *
 * A selected port is classified as short when its Q16 timing is zero and open
 * when it is all ones or has the invalid sign bit. With the reference-resistor
 * topology, T1/T3 yields RTD1 and T2/T4 yields RTD2. Conversion remains optional
 * so raw timing evidence is available even when board resistance metadata is
 * absent.
 */
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

        /* Unselected ports are decoded for evidence but excluded from validity. */
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

    const Max35103Profile *profile = max_active_profile(drv);
    if (profile &&
        profile->reference_resistance_milliohm != 0U) {
        const uint8_t pair1_mask = MAX35103_TEMP_PORT_T1 |
                                   MAX35103_TEMP_PORT_T3;
        const uint8_t pair2_mask = MAX35103_TEMP_PORT_T2 |
                                   MAX35103_TEMP_PORT_T4;

        if ((result->selected_port_mask & pair1_mask) == pair1_mask &&
            (result->valid_port_mask & pair1_mask) == pair1_mask) {
            result->rtd1_resistance_milliohm = max_ratio_to_resistance(
                result->port_q16[0], result->port_q16[2],
                profile->reference_resistance_milliohm);
            result->rtd1_valid = true;
        }
        if ((result->selected_port_mask & pair2_mask) == pair2_mask &&
            (result->valid_port_mask & pair2_mask) == pair2_mask) {
            result->rtd2_resistance_milliohm = max_ratio_to_resistance(
                result->port_q16[1], result->port_q16[3],
                profile->reference_resistance_milliohm);
            result->rtd2_valid = true;
        }
    }

    if (profile && profile->rtd_nominal_resistance_milliohm != 0U) {
        if (result->rtd1_valid &&
            MAX35103_PlatinumRtdToMilliCelsius(
                result->rtd1_resistance_milliohm,
                profile->rtd_nominal_resistance_milliohm,
                &result->rtd1_temperature_millicelsius) == MAX35103_OK) {
            result->rtd1_temperature_valid = true;
        }
        if (result->rtd2_valid &&
            MAX35103_PlatinumRtdToMilliCelsius(
                result->rtd2_resistance_milliohm,
                profile->rtd_nominal_resistance_milliohm,
                &result->rtd2_temperature_millicelsius) == MAX35103_OK) {
            result->rtd2_temperature_valid = true;
        }
    }

    /*
     * Overall timing validity requires every selected port, clean status, and
     * at least one averaged cycle. RTD conversion validity is reported
     * separately and does not hide otherwise usable raw timing.
     */
    result->valid = result->selected_port_mask != 0U &&
                    (result->valid_port_mask & result->selected_port_mask) ==
                        result->selected_port_mask &&
                    (status & (MAX35103_INT_TIMEOUT |
                               MAX35103_INT_POR)) == 0U &&
                    (!averaged || result->valid_cycle_count != 0U);
    return result->valid;
}

/**
 * @brief Read and decode the full 35-word TOF result bank synchronously.
 */
static Max35103Status max_read_tof_words_blocking(
    Max35103Driver *drv,
    uint16_t status,
    uint64_t timestamp_us,
    bool use_average,
    Max35103RawResult *result)
{
    uint8_t bank[MAX35103_TOF_RESULT_BANK_DATA_BYTES];

    if (max_spi_read_block_data(
            drv, MAX35103_REG_WVRUP, bank,
            MAX35103_TOF_RESULT_BANK_WORDS) !=
        MAX35103_TRANSPORT_OK) {
        return MAX35103_SPI_ERROR;
    }

    return max_decode_tof_bank(bank, status, timestamp_us,
                               use_average, result)
           ? MAX35103_OK
           : MAX35103_DEVICE_ERROR;
}

/**
 * @brief Read direct or averaged temperature words synchronously.
 *
 * For averaged data, the leading cycle-count word is separated before the
 * remaining 16 bytes are passed to the common T1..T4 decoder.
 */
static Max35103Status max_read_temperature_words_blocking(
    Max35103Driver *drv, uint16_t status, uint64_t timestamp_us,
    bool averaged, Max35103TemperatureResult *result)
{
    uint8_t frame[MAX35103_TEMP_RESULT_FRAME_BYTES];
    uint16_t cycle_word = 0U;

    if (averaged) {
        uint8_t averaged_block[
            MAX35103_TEMP_AVG_BLOCK_WORDS * 2U];
        if (max_spi_read_block_data(
                drv, MAX35103_REG_TEMP_CYCLE_COUNT,
                averaged_block, MAX35103_TEMP_AVG_BLOCK_WORDS) !=
            MAX35103_TRANSPORT_OK) {
            return MAX35103_SPI_ERROR;
        }
        cycle_word = (uint16_t)(
            ((uint16_t)averaged_block[0] << 8) |
            (uint16_t)averaged_block[1]);
        memcpy(frame, &averaged_block[2],
               MAX35103_TEMP_RESULT_FRAME_BYTES);
    } else if (max_spi_read_block_data(
                   drv, MAX35103_REG_T1_INT, frame,
                   MAX35103_TEMP_RESULT_WORDS) !=
               MAX35103_TRANSPORT_OK) {
        return MAX35103_SPI_ERROR;
    }

    return max_decode_temperature_frame(drv, frame, status, timestamp_us,
                                        averaged, cycle_word, result)
           ? MAX35103_OK
           : MAX35103_DEVICE_ERROR;
}

/**
 * @brief Apply the device's minimum legal T2 wave-selection clamp.
 */
static uint8_t max_effective_t2_wave(const Max35103Profile *profile)
{
    uint8_t wave = (uint8_t)(
        (profile->tof2 & MAX35103_TOF2_T2WV_MASK) >>
        MAX35103_TOF2_T2WV_SHIFT);
    return wave < 2U ? 2U : wave;
}

/**
 * @brief Decode one HIT wave selector and apply its earliest legal wave.
 *
 * HIT1..HIT6 selectors occupy alternating bytes of TOF3..TOF5. The hardware
 * treats values below hit_index+3 as that earliest wave, so validation compares
 * effective rather than raw zero-compatible values.
 */
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

    /*
     * Clear first so even an invalid transport leaves a deterministic UNINIT
     * object rather than partially preserving a prior driver's ownership.
     */
    memset(drv, 0, sizeof(*drv));
    if (!max_transport_valid(transport)) {
        drv->state = MAX35103_STATE_UNINIT;
        return MAX35103_INVALID_ARG;
    }

    drv->transport = *transport;
    drv->state = MAX35103_STATE_IDLE;
    drv->generation = 1U;
    drv->queue_overflow_policy = MAX35103_QUEUE_DROP_OLDEST;
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

    /*
     * Hardware reset destroys the active volatile register image. Invalidate
     * host configuration proof and all in-flight event associations before
     * touching the reset pin.
     */
    drv->generation++;
    drv->state = MAX35103_STATE_ARMING;
    drv->device_ready = false;
    max_invalidate_profile(drv);
    drv->event_timing_active = false;
    drv->expected_event_flags = 0U;
    drv->seen_event_flags = 0U;
    drv->irq_recheck_pending = false;
    MAX35103_ClearResultQueues(drv);
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

    /*
     * POR is read-to-clear evidence that the physical device observed reset.
     * An all-ones value is treated as a disconnected/open MISO bus.
     */
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

    const uint8_t pl = (uint8_t)(
        (profile->tof1 & MAX35103_TOF1_PL_MASK) >>
        MAX35103_TOF1_PL_SHIFT);
    const uint8_t stop_code = (uint8_t)(
        (profile->tof2 & MAX35103_TOF2_STOP_MASK) >>
        MAX35103_TOF2_STOP_SHIFT);

    /*
     * Structural gate: constrain application-supported pulse/HIT values,
     * require at least one DPL bit, and reject every documented reserved bit.
     */
    if (pl < MAX35103_PL_MIN || pl > MAX35103_PL_MAX ||
        (profile->tof1 & MAX35103_TOF1_DPL_MASK) == 0U ||
        stop_code > MAX35103_STOP_CODE_MAX ||
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

    /*
     * Copy first so Configure() remains safe even when the caller passes the
     * address returned from this driver's own active-profile storage.
     */
    const Max35103Profile candidate = *profile;
    const Max35103Status validation =
        MAX35103_ValidateProfile(&candidate);
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
        { MAX35103_REG_TOF1, candidate.tof1 },
        { MAX35103_REG_TOF2, candidate.tof2 },
        { MAX35103_REG_TOF3, candidate.tof3 },
        { MAX35103_REG_TOF4, candidate.tof4 },
        { MAX35103_REG_TOF5, candidate.tof5 },
        { MAX35103_REG_TOF6, candidate.tof6 },
        { MAX35103_REG_TOF7, candidate.tof7 },
        { MAX35103_REG_EVT_TIMING_1, candidate.event_timing_1 },
        { MAX35103_REG_EVT_TIMING_2, candidate.event_timing_2 },
        { MAX35103_REG_TOF_MEAS_DELAY, candidate.tof_measurement_delay },
        { MAX35103_REG_CAL_CTRL, candidate.calibration_control },
    };

    /*
     * Invalidate before the first write. If any later transfer/readback fails,
     * the IC may contain a mixed old/new image and must not be used for
     * configuration-dependent measurements.
     */
    max_invalidate_profile(drv);
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

    /* Commit host ownership only after every write has exact readback proof. */
    drv->active_profile = candidate;
    drv->configured = true;
    drv->profile_synchronized = true;
    return MAX35103_OK;
}

Max35103Status MAX35103_StartEventTiming(Max35103Driver *drv)
{
    if (!drv) {
        return MAX35103_INVALID_ARG;
    }
    const Max35103Profile *profile = max_active_profile(drv);
    if (!drv->device_ready || !profile) {
        return MAX35103_NOT_READY;
    }
    if (drv->state != MAX35103_STATE_IDLE || drv->event_timing_active ||
        drv->spi_pending) {
        return MAX35103_BUSY;
    }
    if (!max_is_execution_opcode(profile->event_mode_cmd)) {
        return MAX35103_CONFIG_ERROR;
    }
    /*
     * Deferred processing depends on MAX_INT. Starting with INT_EN clear would
     * leave EVENT_RUNNING without completion evidence or a useful deadline.
     */
    if ((profile->calibration_control &
         MAX35103_CAL_CTRL_INT_EN) == 0U) {
        return MAX35103_CONFIG_ERROR;
    }

    if (max_spi_command(drv, profile->event_mode_cmd) !=
        MAX35103_TRANSPORT_OK) {
        max_enter_error(drv);
        return MAX35103_SPI_ERROR;
    }

    /*
     * Clear stale timestamps/tokens only after the command transfer succeeds;
     * then publish the expected flag set before entering EVENT_RUNNING.
     */
    max_clear_operation(drv);
    drv->event_timing_active = true;
    drv->expected_event_flags = max_expected_event_flags(drv);
    drv->seen_event_flags = 0U;
    drv->irq_recheck_pending = false;
    drv->pending_irq_timestamp_us = 0U;
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
        drv->expected_event_flags = 0U;
        drv->seen_event_flags = 0U;
        drv->irq_recheck_pending = false;
        if (drv->state != MAX35103_STATE_ERROR) {
            drv->state = MAX35103_STATE_IDLE;
        }
        return MAX35103_OK;
    }

    /*
     * HALT takes ownership away from any deferred result read. Cancel host DMA
     * first so INT_STATUS polling cannot race with a previous transaction.
     */
    drv->generation++;
    max_cancel_pending_spi(drv);
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
            drv->expected_event_flags = 0U;
            drv->seen_event_flags = 0U;
            drv->irq_recheck_pending = false;
            drv->state = MAX35103_STATE_IDLE;
            max_clear_operation(drv);
            return MAX35103_OK;
        }
        if ((status & MAX35103_INT_POR) != 0U) {
            drv->device_ready = false;
            max_invalidate_profile(drv);
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
        drv->spi_pending) {
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
        if (status == 0xFFFFU) {
            drv->device_ready = false;
            max_invalidate_profile(drv);
            max_enter_error(drv);
            return MAX35103_DEVICE_ERROR;
        }

        if ((status & MAX35103_INT_TOF_COMPLETE) != 0U) {
            /*
             * Direct TOF_DIFF selects E2/E3 rather than EVTMG average E5/E6.
             * Publish invalid decoded evidence too, so diagnostics can inspect
             * a failed self-check instead of losing the snapshot.
             */
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
                max_invalidate_profile(drv);
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
    if (!drv->device_ready || !max_active_profile(drv)) {
        return MAX35103_NOT_READY;
    }
    if (drv->state != MAX35103_STATE_IDLE || drv->event_timing_active ||
        drv->spi_pending) {
        return MAX35103_BUSY;
    }

    memset(result, 0, sizeof(*result));
    /*
     * Allocate before issuing the command so every completion/error path for
     * this attempt carries one stable identity.
     */
    const uint32_t measurement_sequence = max_allocate_sequence(drv);
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
            result->sequence_number = measurement_sequence;
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
                result->sequence_number = measurement_sequence;
                result->selected_port_mask =
                    max_selected_temperature_ports(drv);
                drv->device_ready = false;
                max_invalidate_profile(drv);
                max_enter_error(drv);
            } else {
                const Max35103Status read_status =
                    max_read_temperature_words_blocking(
                        drv, status, 0U, false, result);
                result->sequence_number = measurement_sequence;
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

    /*
     * Cancel is host-side cleanup only. Preserve event_timing_active because
     * the IC continues running until MAX35103_Halt() sends a device command.
     */
    drv->generation++;
    max_clear_operation(drv);
    MAX35103_ClearResultQueues(drv);
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
    if (!drv->device_ready || !drv->event_timing_active) {
        drv->unexpected_irq_count++;
        return;
    }

    /*
     * A second edge cannot start another SPI frame while one is pending.
     * Preserve only the need to reread self-clearing status plus the most
     * recent timestamp; completion will drain it after the current snapshot.
     */
    if (drv->state != MAX35103_STATE_EVENT_RUNNING || drv->spi_pending) {
        if (drv->state == MAX35103_STATE_DRAIN_STATUS ||
            drv->state == MAX35103_STATE_READ_RESULT ||
            drv->state == MAX35103_STATE_READ_TEMP_RESULT ||
            drv->spi_pending) {
            drv->irq_recheck_pending = true;
            drv->pending_irq_timestamp_us = now_us;
            drv->irq_recheck_count++;
        } else {
            drv->unexpected_irq_count++;
        }
        return;
    }

    if (!max_begin_status_drain(drv, now_us)) {
        max_enter_error(drv);
    }
}

bool MAX35103_GetPendingSpiRequest(Max35103Driver *drv,
                                   Max35103SpiRequest *request)
{
    if (!drv || !request || !drv->spi_pending) {
        return false;
    }

    /* Return borrowed views; copying frame data is intentionally avoided. */
    request->tx = drv->tx_buf;
    request->rx = drv->rx_buf;
    request->length = drv->spi_length;
    request->token = drv->spi_token;
    return true;
}

Max35103Status MAX35103_StartPendingSpiAsync(Max35103Driver *drv)
{
    if (!drv) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->spi_pending) {
        return MAX35103_NOT_READY;
    }
    if (drv->spi_async_active) {
        return MAX35103_BUSY;
    }
    if (drv->transport.start_transfer_async == NULL ||
        drv->transport.cancel_transfer_async == NULL) {
        return MAX35103_NOT_READY;
    }

    const Max35103TransportStatus lock_status = max_bus_acquire(drv);
    if (lock_status != MAX35103_TRANSPORT_OK) {
        return lock_status == MAX35103_TRANSPORT_BUSY
               ? MAX35103_BUSY
               : (lock_status == MAX35103_TRANSPORT_TIMEOUT
                  ? MAX35103_TIMEOUT
                  : MAX35103_SPI_ERROR);
    }

    /*
     * Mark active before the platform start hook, which may enable an IRQ that
     * completes immediately. A failed start rolls back both flag and bus lock.
     */
    drv->spi_async_active = true;
    const Max35103TransportStatus start_status =
        drv->transport.start_transfer_async(
            drv->transport.context, drv->tx_buf, drv->rx_buf,
            drv->spi_length, drv->spi_token);
    if (start_status == MAX35103_TRANSPORT_OK) {
        drv->dma_start_count++;
        return MAX35103_OK;
    }

    drv->spi_async_active = false;
    max_bus_release(drv);
    if (start_status != MAX35103_TRANSPORT_BUSY) {
        drv->dma_error_count++;
    }
    return start_status == MAX35103_TRANSPORT_BUSY
           ? MAX35103_BUSY
           : (start_status == MAX35103_TRANSPORT_TIMEOUT
              ? MAX35103_TIMEOUT
              : MAX35103_SPI_ERROR);
}

void MAX35103_OnSpiDone(Max35103Driver *drv, uint32_t token,
                        bool transfer_ok)
{
    if (!drv) {
        return;
    }
    /*
     * Ignore stale callbacks from an aborted/reinitialized DMA generation.
     * They must never release the bus or advance a newer pending request.
     */
    if (!drv->spi_pending || token == 0U || token != drv->spi_token) {
        drv->stale_spi_completion_count++;
        return;
    }

    const bool completed_async = drv->spi_async_active;
    if (completed_async) {
        drv->spi_async_active = false;
        max_bus_release(drv);
    }
    max_clear_pending_spi(drv);
    drv->spi_done_count++;

    if (!transfer_ok) {
        /* A partial frame cannot yield a trustworthy register-bank snapshot. */
        if (completed_async) {
            drv->dma_error_count++;
        }
        max_enter_error(drv);
        return;
    }

    switch (drv->state) {
    case MAX35103_STATE_DRAIN_STATUS: {
        /*
         * INT_STATUS arrived in bytes 1..2 because byte 0 was shifted while
         * sending the opcode. Latch once: later result reads must not consume
         * status again because the register is self-clearing.
         */
        const uint16_t status = (uint16_t)(
            ((uint16_t)drv->rx_buf[1] << 8) |
            (uint16_t)drv->rx_buf[2]);
        drv->latched_status = status;

        if (status == 0xFFFFU) {
            drv->device_ready = false;
            max_invalidate_profile(drv);
            drv->event_timing_active = false;
            max_enter_error(drv);
            return;
        }
        if ((status & MAX35103_INT_POR) != 0U) {
            if ((drv->expected_event_flags &
                 MAX35103_INT_TOF_EVTMG) != 0U) {
                max_publish_status_only(drv, status,
                                        drv->interrupt_timestamp_us);
            }
            if ((drv->expected_event_flags &
                 MAX35103_INT_TEMP_EVTMG) != 0U) {
                max_publish_temperature_status_only(
                    drv, status, drv->interrupt_timestamp_us);
            }
            drv->device_ready = false;
            max_invalidate_profile(drv);
            drv->event_timing_active = false;
            max_enter_error(drv);
            return;
        }

        const uint16_t tof_ready = MAX35103_INT_TOF_COMPLETE |
                                   MAX35103_INT_TOF_EVTMG;
        const uint16_t temperature_ready = MAX35103_INT_TEMP_COMPLETE |
                                            MAX35103_INT_TEMP_EVTMG;
        /*
         * Accumulate only flags required by the configured event mode. Other
         * completion bits remain visible in latched_status but do not satisfy
         * this event's pairing contract.
         */
        drv->seen_event_flags |=
            (uint16_t)(status & drv->expected_event_flags);
        if ((status & tof_ready) != 0U) {
            drv->state = MAX35103_STATE_READ_RESULT;
            drv->result_word_index = 0U;
            memset(drv->result_frame, 0, sizeof(drv->result_frame));
            if (!max_schedule_block_read(
                    drv, MAX35103_REG_WVRUP,
                    MAX35103_TOF_RESULT_BANK_WORDS)) {
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

        max_finish_event_interrupt(drv, status);
        return;
    }

    case MAX35103_STATE_READ_RESULT: {
        /* Skip rx[0] (opcode phase) and preserve the complete 70-byte bank. */
        memcpy(drv->result_frame, &drv->rx_buf[1],
               MAX35103_TOF_RESULT_BANK_DATA_BYTES);

        Max35103RawResult decoded;
        const bool use_average =
            (drv->latched_status & MAX35103_INT_TOF_EVTMG) != 0U;
        (void)max_decode_tof_bank(
            drv->result_frame, drv->latched_status,
            drv->interrupt_timestamp_us, use_average, &decoded);
        max_publish_result(drv, &decoded);

        /*
         * EVTMG1 can announce TOF and temperature in the same status snapshot.
         * Decode/publish TOF first, then reuse the same sequence for temperature.
         */
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
        if (averaged) {
            /* Averaged frame layout: opcode, cycle word, then 16 data bytes. */
            drv->temperature_cycle_word = (uint16_t)(
                ((uint16_t)drv->rx_buf[1] << 8) |
                (uint16_t)drv->rx_buf[2]);
            memcpy(drv->temperature_frame, &drv->rx_buf[3],
                   MAX35103_TEMP_RESULT_FRAME_BYTES);
        } else {
            memcpy(drv->temperature_frame, &drv->rx_buf[1],
                   MAX35103_TEMP_RESULT_FRAME_BYTES);
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

    /*
     * Keep a local copy of the token because OnSpiDone() clears pending request
     * metadata as part of normal completion.
     */
    const Max35103TransportStatus transport_status = max_spi_xfer(
        drv, request.tx, request.rx, request.length);
    if (transport_status == MAX35103_TRANSPORT_BUSY) {
        return MAX35103_BUSY;
    }
    MAX35103_OnSpiDone(
        drv, request.token,
        transport_status == MAX35103_TRANSPORT_OK);
    if (transport_status == MAX35103_TRANSPORT_OK) {
        return MAX35103_OK;
    }
    return transport_status == MAX35103_TRANSPORT_TIMEOUT
           ? MAX35103_TIMEOUT
           : MAX35103_SPI_ERROR;
}

Max35103Status MAX35103_Process(Max35103Driver *drv, uint64_t now_us)
{
    if (!drv) {
        return MAX35103_INVALID_ARG;
    }

    /*
     * No deadline runs while merely waiting for the first event edge. Once any
     * required flag is observed, the deadline remains active until every part
     * of the event is drained or timeout publishes missing records.
     */
    if ((drv->state == MAX35103_STATE_DRAIN_STATUS ||
         drv->state == MAX35103_STATE_READ_RESULT ||
         drv->state == MAX35103_STATE_READ_TEMP_RESULT ||
         (drv->state == MAX35103_STATE_EVENT_RUNNING &&
          drv->seen_event_flags != 0U)) &&
        drv->deadline_us != 0U && now_us >= drv->deadline_us) {
        MAX35103_OnTimeout(drv);
        return MAX35103_TIMEOUT;
    }

    if (drv->spi_pending) {
        if (drv->transport.start_transfer_async != NULL) {
            if (drv->spi_async_active) {
                return MAX35103_BUSY;
            }
            const Max35103Status start_status =
                MAX35103_StartPendingSpiAsync(drv);
            if (start_status == MAX35103_SPI_ERROR ||
                start_status == MAX35103_TIMEOUT) {
                max_enter_error(drv);
            }
            return start_status;
        }
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
         drv->state != MAX35103_STATE_READ_TEMP_RESULT &&
         drv->state != MAX35103_STATE_EVENT_RUNNING)) {
        return;
    }

    /* Preserve all latched device evidence and add a host timeout indication. */
    const uint16_t status = (uint16_t)(drv->latched_status |
                                       MAX35103_INT_TIMEOUT);
    if (drv->state == MAX35103_STATE_READ_RESULT) {
        max_publish_status_only(
            drv, status, drv->interrupt_timestamp_us);
        drv->seen_event_flags |= MAX35103_INT_TOF_EVTMG;
    } else if (drv->state == MAX35103_STATE_READ_TEMP_RESULT) {
        max_publish_temperature_status_only(
            drv, status, drv->interrupt_timestamp_us);
        drv->seen_event_flags |= MAX35103_INT_TEMP_EVTMG;
    }
    max_enter_event_timeout(drv, status);
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
    /*
     * Reading INT_STATUS is destructive. Other registers may be diagnosed
     * while event timing waits, but status remains exclusively owned by FSM.
     */
    if (read_opcode == MAX35103_REG_INT_STATUS &&
        max_int_status_owned(drv)) {
        return MAX35103_BUSY;
    }

    return max_spi_read_reg(drv, read_opcode, value) ==
               MAX35103_TRANSPORT_OK
           ? MAX35103_OK
           : MAX35103_SPI_ERROR;
}

Max35103Status MAX35103_ReadBlock(
    Max35103Driver *drv, uint8_t start_read_opcode,
    uint16_t *words, uint8_t word_count)
{
    if (!drv || !words ||
        !max_block_range_valid(start_read_opcode, word_count)) {
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
    /*
     * A block that reaches INT_STATUS is also destructive, even when its start
     * opcode names an earlier result register.
     */
    const uint16_t end_opcode = (uint16_t)(
        (uint16_t)start_read_opcode + (uint16_t)word_count - 1U);
    if (max_int_status_owned(drv) &&
        start_read_opcode != MAX35103_REG_CONTROL &&
        end_opcode >= MAX35103_REG_INT_STATUS) {
        return MAX35103_BUSY;
    }

    uint8_t data[MAX35103_MAX_BLOCK_WORDS * 2U];
    if (max_spi_read_block_data(
            drv, start_read_opcode, data, word_count) !=
        MAX35103_TRANSPORT_OK) {
        return MAX35103_SPI_ERROR;
    }

    for (uint8_t index = 0U; index < word_count; ++index) {
        const uint16_t byte_index = (uint16_t)index * 2U;
        words[index] = (uint16_t)(
            ((uint16_t)data[byte_index] << 8) |
            (uint16_t)data[byte_index + 1U]);
    }
    return MAX35103_OK;
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

    if (!max_direct_pl_stop_valid(write_opcode, value)) {
        return MAX35103_CONFIG_ERROR;
    }

    if (max_profile_synchronized(drv)) {
        Max35103Profile candidate = drv->active_profile;
        if (max_update_profile_register(
                &candidate, write_opcode, value) &&
            MAX35103_ValidateProfile(&candidate) != MAX35103_OK) {
            return MAX35103_CONFIG_ERROR;
        }
    }

    if (max_spi_write_reg(drv, write_opcode, value) !=
        MAX35103_TRANSPORT_OK) {
        if (max_is_configuration_write(write_opcode)) {
            max_invalidate_profile(drv);
        }
        return MAX35103_SPI_ERROR;
    }

    /*
     * A successful transfer does not prove that the register accepted the
     * value, so unverified configuration writes always invalidate the shadow.
     */
    if (max_is_configuration_write(write_opcode)) {
        max_invalidate_profile(drv);
    }
    return MAX35103_OK;
}

Max35103Status MAX35103_WriteVerifyReg(Max35103Driver *drv,
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
    if (!max_direct_pl_stop_valid(write_opcode, value)) {
        return MAX35103_CONFIG_ERROR;
    }

    /*
     * Build a candidate shadow before touching hardware. This prevents a
     * verified individual write from creating a structurally invalid profile.
     */
    const bool was_synchronized = max_profile_synchronized(drv);
    bool profile_register = false;
    Max35103Profile candidate;
    memset(&candidate, 0, sizeof(candidate));
    if (was_synchronized) {
        candidate = drv->active_profile;
        profile_register = max_update_profile_register(
            &candidate, write_opcode, value);
        if (profile_register &&
            MAX35103_ValidateProfile(&candidate) != MAX35103_OK) {
            return MAX35103_CONFIG_ERROR;
        }
    }

    if (max_spi_write_reg(drv, write_opcode, value) !=
        MAX35103_TRANSPORT_OK) {
        if (max_is_configuration_write(write_opcode)) {
            max_invalidate_profile(drv);
        }
        return MAX35103_SPI_ERROR;
    }

    uint16_t readback = 0U;
    if (max_spi_read_reg(
            drv, max_readback_opcode(write_opcode), &readback) !=
        MAX35103_TRANSPORT_OK) {
        if (max_is_configuration_write(write_opcode)) {
            max_invalidate_profile(drv);
        }
        return MAX35103_SPI_ERROR;
    }
    if (readback != value) {
        if (max_is_configuration_write(write_opcode)) {
            max_invalidate_profile(drv);
        }
        return MAX35103_CONFIG_ERROR;
    }

    if (max_is_configuration_write(write_opcode)) {
        /*
         * Exact register readback is necessary but not sufficient for complete
         * profile ownership. Preserve synchronization only if this register is
         * represented in a previously synchronized, revalidated profile.
         */
        if (was_synchronized && profile_register) {
            drv->active_profile = candidate;
            drv->configured = true;
            drv->profile_synchronized = true;
        } else {
            max_invalidate_profile(drv);
        }
    }
    return MAX35103_OK;
}

bool MAX35103_IsProfileSynchronized(const Max35103Driver *drv)
{
    return max_profile_synchronized(drv);
}

Max35103Status MAX35103_GetActiveProfile(
    const Max35103Driver *drv, Max35103Profile *profile)
{
    if (!drv || !profile) {
        return MAX35103_INVALID_ARG;
    }
    if (!drv->device_ready) {
        return MAX35103_NOT_READY;
    }
    if (!max_profile_synchronized(drv)) {
        return MAX35103_STALE;
    }

    *profile = drv->active_profile;
    return MAX35103_OK;
}

/* -------------------------------------------------------------------------- */
/* Result and status access                                                   */
/* -------------------------------------------------------------------------- */

bool MAX35103_HasResult(const Max35103Driver *drv)
{
    return MAX35103_ResultAvailable(drv) != 0U;
}

Max35103Status MAX35103_GetResult(Max35103Driver *drv,
                                  Max35103RawResult *result)
{
    return MAX35103_ResultPop(drv, result);
}

size_t MAX35103_ResultAvailable(const Max35103Driver *drv)
{
    return drv ? (size_t)drv->result_queue_count : 0U;
}

Max35103Status MAX35103_ResultPop(
    Max35103Driver *drv, Max35103RawResult *result)
{
    if (!drv || !result) {
        return MAX35103_INVALID_ARG;
    }
    if (drv->result_queue_count == 0U) {
        return MAX35103_NO_RESULT;
    }

    /*
     * Copy before zeroing the slot. Clearing removed data makes debugger dumps
     * reflect current FIFO ownership and prevents accidental stale inspection.
     */
    *result = drv->result_queue[drv->result_queue_head];
    memset(&drv->result_queue[drv->result_queue_head], 0,
           sizeof(drv->result_queue[drv->result_queue_head]));
    drv->result_queue_head = (uint8_t)(
        ((uint16_t)drv->result_queue_head + 1U) %
        MAX35103_RESULT_QUEUE_CAPACITY);
    drv->result_queue_count--;
    return MAX35103_OK;
}

bool MAX35103_HasTemperatureResult(const Max35103Driver *drv)
{
    return MAX35103_TemperatureResultAvailable(drv) != 0U;
}

Max35103Status MAX35103_GetTemperatureResult(
    Max35103Driver *drv, Max35103TemperatureResult *result)
{
    return MAX35103_TemperatureResultPop(drv, result);
}

size_t MAX35103_TemperatureResultAvailable(const Max35103Driver *drv)
{
    return drv ? (size_t)drv->temperature_queue_count : 0U;
}

Max35103Status MAX35103_TemperatureResultPop(
    Max35103Driver *drv, Max35103TemperatureResult *result)
{
    if (!drv || !result) {
        return MAX35103_INVALID_ARG;
    }
    if (drv->temperature_queue_count == 0U) {
        return MAX35103_NO_RESULT;
    }

    /* Same bounded-ring semantics as the TOF queue. */
    *result = drv->temperature_queue[drv->temperature_queue_head];
    memset(&drv->temperature_queue[drv->temperature_queue_head], 0,
           sizeof(drv->temperature_queue[drv->temperature_queue_head]));
    drv->temperature_queue_head = (uint8_t)(
        ((uint16_t)drv->temperature_queue_head + 1U) %
        MAX35103_TEMPERATURE_QUEUE_CAPACITY);
    drv->temperature_queue_count--;
    return MAX35103_OK;
}

Max35103Status MAX35103_SetQueueOverflowPolicy(
    Max35103Driver *drv, Max35103QueueOverflowPolicy policy)
{
    if (!drv ||
        (policy != MAX35103_QUEUE_DROP_OLDEST &&
         policy != MAX35103_QUEUE_DROP_NEWEST)) {
        return MAX35103_INVALID_ARG;
    }
    drv->queue_overflow_policy = policy;
    return MAX35103_OK;
}

void MAX35103_ClearResultQueues(Max35103Driver *drv)
{
    if (!drv) {
        return;
    }

    /* Queue diagnostics are lifetime counters and intentionally remain intact. */
    memset(drv->result_queue, 0, sizeof(drv->result_queue));
    memset(drv->temperature_queue, 0, sizeof(drv->temperature_queue));
    drv->result_queue_head = 0U;
    drv->result_queue_count = 0U;
    drv->temperature_queue_head = 0U;
    drv->temperature_queue_count = 0U;
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
    if (drv->spi_pending || drv->event_timing_active ||
        drv->state != MAX35103_STATE_IDLE) {
        return MAX35103_BUSY;
    }

    /*
     * This standalone path intentionally consumes INT_STATUS once, so it is
     * allowed only when deferred event timing does not own the register.
     */
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
            result->sequence_number = max_allocate_sequence(drv);
            return MAX35103_DEVICE_ERROR;
        }
        return MAX35103_NO_RESULT;
    }

    const bool use_average =
        (status & MAX35103_INT_TOF_EVTMG) != 0U;
    const Max35103Status read_status = max_read_tof_words_blocking(
        drv, status, 0U, use_average, result);
    result->sequence_number = max_allocate_sequence(drv);
    return read_status;
}

uint8_t MAX35103_ConfiguredHitCount(const Max35103Profile *profile)
{
    if (!profile) {
        return 0U;
    }

    const uint8_t stop_code = (uint8_t)(
        (profile->tof2 & MAX35103_TOF2_STOP_MASK) >>
        MAX35103_TOF2_STOP_SHIFT);
    if (stop_code > MAX35103_STOP_CODE_MAX) {
        return 0U;
    }
    return (uint8_t)(stop_code + 1U);
}

Max35103Status MAX35103_ReadWaveEvidence(
    Max35103Driver *drv, Max35103WaveEvidence *evidence)
{
    if (!drv || !evidence) {
        return MAX35103_INVALID_ARG;
    }
    const Max35103Profile *profile = max_active_profile(drv);
    if (!drv->device_ready || !profile) {
        return MAX35103_NOT_READY;
    }
    if (MAX35103_IsBusy(drv) || drv->event_timing_active) {
        return MAX35103_BUSY;
    }

    memset(evidence, 0, sizeof(*evidence));
    evidence->configured_hit_count =
        MAX35103_ConfiguredHitCount(profile);
    if (evidence->configured_hit_count == 0U) {
        return MAX35103_CONFIG_ERROR;
    }

    /*
     * Capture all WVR/HIT/AVG registers in one sequential transaction. Reading
     * words separately could mix evidence from different measurement updates.
     */
    uint8_t bank[MAX35103_TOF_RESULT_BANK_DATA_BYTES];
    if (max_spi_read_block_data(
            drv, MAX35103_REG_WVRUP, bank,
            MAX35103_TOF_RESULT_BANK_WORDS) !=
        MAX35103_TRANSPORT_OK) {
        return MAX35103_SPI_ERROR;
    }

    evidence->wvr_up =
        max_tof_bank_word(bank, MAX35103_REG_WVRUP);
    evidence->wvr_down =
        max_tof_bank_word(bank, MAX35103_REG_WVRDN);
    evidence->wvr_up_t1_t2_q7 = (uint8_t)(evidence->wvr_up >> 8);
    evidence->wvr_up_t2_ideal_q7 = (uint8_t)evidence->wvr_up;
    evidence->wvr_down_t1_t2_q7 =
        (uint8_t)(evidence->wvr_down >> 8);
    evidence->wvr_down_t2_ideal_q7 =
        (uint8_t)evidence->wvr_down;

    /*
     * WVR all-ones indicates an invalid/open-bus value; zero sub-ratios provide
     * no usable wave-shape evidence for calibration.
     */
    bool valid = evidence->wvr_up != 0xFFFFU &&
                 evidence->wvr_down != 0xFFFFU &&
                 evidence->wvr_up_t1_t2_q7 != 0U &&
                 evidence->wvr_up_t2_ideal_q7 != 0U &&
                 evidence->wvr_down_t1_t2_q7 != 0U &&
                 evidence->wvr_down_t2_ideal_q7 != 0U;
    uint32_t previous_up = 0U;
    uint32_t previous_down = 0U;
    uint64_t hit_up_sum = 0U;
    uint64_t hit_down_sum = 0U;

    for (uint8_t hit = 0U;
         hit < evidence->configured_hit_count;
         ++hit) {
        const uint8_t up_int_opcode =
            (uint8_t)(MAX35103_REG_HIT1UP_INT + hit * 2U);
        const uint8_t down_int_opcode =
            (uint8_t)(MAX35103_REG_HIT1DN_INT + hit * 2U);
        evidence->hit_up_int[hit] =
            max_tof_bank_word(bank, up_int_opcode);
        evidence->hit_up_frac[hit] =
            max_tof_bank_word(bank, (uint8_t)(up_int_opcode + 1U));
        evidence->hit_down_int[hit] =
            max_tof_bank_word(bank, down_int_opcode);
        evidence->hit_down_frac[hit] =
            max_tof_bank_word(bank, (uint8_t)(down_int_opcode + 1U));

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

        /*
         * Every selected HIT must be positive, non-sentinel, and strictly later
         * than the previous HIT in both directions.
         */
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
        hit_up_sum += evidence->hit_up_q16[hit];
        hit_down_sum += evidence->hit_down_q16[hit];
    }

    /*
     * Recalculate the mean with nearest-integer rounding and require exact
     * equality to hardware AVGUP/AVGDN. This is a bank-coherence gate used by
     * characterization/auto-calibration, not the normal production FIFO path.
     */
    const uint32_t rounded_hit_up_average = (uint32_t)(
        (hit_up_sum + evidence->configured_hit_count / 2U) /
        evidence->configured_hit_count);
    const uint32_t rounded_hit_down_average = (uint32_t)(
        (hit_down_sum + evidence->configured_hit_count / 2U) /
        evidence->configured_hit_count);
    evidence->avg_up_q16 =
        ((uint32_t)max_tof_bank_word(
             bank, MAX35103_REG_AVGUP_INT) << 16) |
        max_tof_bank_word(bank, MAX35103_REG_AVGUP_FRAC);
    evidence->avg_down_q16 =
        ((uint32_t)max_tof_bank_word(
             bank, MAX35103_REG_AVGDN_INT) << 16) |
        max_tof_bank_word(bank, MAX35103_REG_AVGDN_FRAC);
    evidence->avg_up_consistent =
        evidence->avg_up_q16 == rounded_hit_up_average;
    evidence->avg_down_consistent =
        evidence->avg_down_q16 == rounded_hit_down_average;
    valid = valid &&
            evidence->avg_up_consistent &&
            evidence->avg_down_consistent;
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

    /*
     * SPI has no ACK and MAX35103 has no identity register. Reject common
     * disconnected-bus values and impossible reserved bits; this remains a
     * presence heuristic rather than cryptographic/device-ID proof.
     */
    if (tof1 == 0x0000U || tof1 == 0xFFFFU ||
        control == 0xFFFFU ||
        (tof1 & 0x0004U) != 0U ||
        (control & 0xF000U) != 0U) {
        return false;
    }
    return true;
}
