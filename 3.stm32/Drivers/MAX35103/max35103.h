/**
  ******************************************************************************
  * @file    max35103.h
  * @brief   Portable MAX35103 time-of-flight and temperature driver
  ******************************************************************************
  *
  * The MAX35103 uses an opcode-based, 4-wire SPI protocol. Execution commands
  * are one byte; register reads and writes are one opcode plus one 16-bit word.
  * SPI mode 1 (CPOL = 0, CPHA = 1), MSB first. A 10 MHz SPI clock is safe over
  * the complete supported supply-voltage range. Platform access is injected
  * through Max35103Transport; the core does not include STM32 HAL headers.
  *
  * The driver never writes the MAX35103 configuration flash. Configuration is
  * applied to the active register image and must be restored after a reset.
  *
  * MAX_INT is active-low/open-drain. Configure the STM32 input with a pull-up
  * and a falling-edge EXTI. Call MAX35103_OnInt() from the deferred event path,
  * not from code that performs a blocking SPI transaction inside the ISR.
  * Serialize OnInt(), OnSpiDone(), Process(), and queue consumers in one worker
  * context; the portable core does not embed an RTOS critical-section policy.
  *
  * @details
  * The module is split into two execution paths:
  * - Blocking control/diagnostic operations configure, reset, probe, or read
  *   the device while the state machine is idle.
  * - Deferred event processing records MAX_INT, schedules register reads, and
  *   publishes decoded TOF/temperature snapshots into bounded FIFOs.
  *
  * All multi-byte register values are transferred most-significant byte first.
  * Raw time values use the MAX35103 Q16 representation: the integer register
  * contains whole 4 MHz reference-clock periods and the fractional register
  * contains fractions of one period in units of 1/65536.
  *
  * @note This driver validates and transports measurements; it does not convert
  *       differential TOF into flow rate. Pipe geometry, acoustic velocity,
  *       zero-flow offset, and production calibration belong to the measurement
  *       processing layer.
  * @warning Reading INT_STATUS clears the device register. While an event is
  *          active, only the internal event FSM may consume this register.
  ******************************************************************************
  */

#ifndef SWFPM_MAX35103_H
#define SWFPM_MAX35103_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @name SPI frame and result-bank dimensions
 *
 * Values are expressed in bytes or 16-bit words as indicated by the suffix.
 * A sequential read contains one opcode byte followed by two data bytes per
 * register word, all under one continuous NSS-low interval.
 * @{
 */
#define MAX35103_REGISTER_FRAME_BYTES          3U
/* Legacy compact-result constants retained for source compatibility. */
#define MAX35103_TOF_RESULT_WORDS               7U
#define MAX35103_TOF_RESULT_FRAME_BYTES        14U
#define MAX35103_WAVE_HIT_COUNT                 6U
#define MAX35103_TEMP_PORT_COUNT                4U
#define MAX35103_TEMP_RESULT_WORDS              8U
#define MAX35103_TEMP_RESULT_FRAME_BYTES       16U
#define MAX35103_TEMP_AVG_BLOCK_WORDS           9U
#define MAX35103_TOF_RESULT_BANK_WORDS         35U
#define MAX35103_TOF_RESULT_BANK_DATA_BYTES    70U
#define MAX35103_MAX_BLOCK_WORDS               \
    MAX35103_TOF_RESULT_BANK_WORDS
#define MAX35103_MAX_SPI_FRAME_BYTES           \
    (1U + 2U * MAX35103_MAX_BLOCK_WORDS)
/** @} */

/*
 * Queue depth can be overridden by the firmware build before including this
 * header. A depth of eight keeps short application stalls from losing event
 * results while keeping the driver instance suitable for an STM32L4.
 */
#ifndef MAX35103_RESULT_QUEUE_CAPACITY
#define MAX35103_RESULT_QUEUE_CAPACITY           8U
#endif
#ifndef MAX35103_TEMPERATURE_QUEUE_CAPACITY
#define MAX35103_TEMPERATURE_QUEUE_CAPACITY      8U
#endif

#if MAX35103_RESULT_QUEUE_CAPACITY < 1U || \
    MAX35103_RESULT_QUEUE_CAPACITY > 255U
#error "MAX35103_RESULT_QUEUE_CAPACITY must be in the range 1..255"
#endif
#if MAX35103_TEMPERATURE_QUEUE_CAPACITY < 1U || \
    MAX35103_TEMPERATURE_QUEUE_CAPACITY > 255U
#error "MAX35103_TEMPERATURE_QUEUE_CAPACITY must be in the range 1..255"
#endif

/**
 * @name Execution command opcodes
 *
 * Each command is sent as a one-byte, NSS-low SPI transaction. These values
 * are device commands, not register addresses.
 * @{
 */
#define MAX35103_CMD_TOF_UP              0x00U
#define MAX35103_CMD_TOF_DOWN            0x01U
#define MAX35103_CMD_TOF_DIFF            0x02U
#define MAX35103_CMD_TEMPERATURE         0x03U
#define MAX35103_CMD_RESET               0x04U
#define MAX35103_CMD_INIT                0x05U
#define MAX35103_CMD_CONFIG_TO_FLASH     0x06U
#define MAX35103_CMD_EVTMG1              0x07U
#define MAX35103_CMD_EVTMG2              0x08U
#define MAX35103_CMD_EVTMG3              0x09U
#define MAX35103_CMD_HALT                0x0AU
#define MAX35103_CMD_LDO_TIMED           0x0BU
#define MAX35103_CMD_LDO_ON              0x0CU
#define MAX35103_CMD_LDO_OFF             0x0DU
#define MAX35103_CMD_CALIBRATE           0x0EU
/** @} */

/* Configuration write opcodes. Readback opcode = write opcode | 0x80. */
#define MAX35103_REG_TOF1                0x38U
#define MAX35103_REG_TOF2                0x39U
#define MAX35103_REG_TOF3                0x3AU
#define MAX35103_REG_TOF4                0x3BU
#define MAX35103_REG_TOF5                0x3CU
#define MAX35103_REG_TOF6                0x3DU
#define MAX35103_REG_TOF7                0x3EU
#define MAX35103_REG_EVT_TIMING_1        0x3FU
#define MAX35103_REG_EVT_TIMING_2        0x40U
#define MAX35103_REG_TOF_MEAS_DELAY      0x41U
#define MAX35103_REG_CAL_CTRL            0x42U

/* Configuration read opcodes. */
#define MAX35103_READ_TOF1               0xB8U
#define MAX35103_READ_TOF2               0xB9U
#define MAX35103_READ_TOF3               0xBAU
#define MAX35103_READ_TOF4               0xBBU
#define MAX35103_READ_TOF5               0xBCU
#define MAX35103_READ_TOF6               0xBDU
#define MAX35103_READ_TOF7               0xBEU
#define MAX35103_READ_EVT_TIMING_1       0xBFU
#define MAX35103_READ_EVT_TIMING_2       0xC0U
#define MAX35103_READ_TOF_MEAS_DELAY     0xC1U
#define MAX35103_READ_CAL_CTRL           0xC2U

/* Per-wave TOF evidence read opcodes. */
#define MAX35103_REG_WVRUP               0xC4U
#define MAX35103_REG_HIT1UP_INT          0xC5U
#define MAX35103_REG_HIT1UP_FRAC         0xC6U
#define MAX35103_REG_HIT2UP_INT          0xC7U
#define MAX35103_REG_HIT2UP_FRAC         0xC8U
#define MAX35103_REG_HIT3UP_INT          0xC9U
#define MAX35103_REG_HIT3UP_FRAC         0xCAU
#define MAX35103_REG_HIT4UP_INT          0xCBU
#define MAX35103_REG_HIT4UP_FRAC         0xCCU
#define MAX35103_REG_HIT5UP_INT          0xCDU
#define MAX35103_REG_HIT5UP_FRAC         0xCEU
#define MAX35103_REG_HIT6UP_INT          0xCFU
#define MAX35103_REG_HIT6UP_FRAC         0xD0U
#define MAX35103_REG_WVRDN               0xD3U
#define MAX35103_REG_HIT1DN_INT          0xD4U
#define MAX35103_REG_HIT1DN_FRAC         0xD5U
#define MAX35103_REG_HIT2DN_INT          0xD6U
#define MAX35103_REG_HIT2DN_FRAC         0xD7U
#define MAX35103_REG_HIT3DN_INT          0xD8U
#define MAX35103_REG_HIT3DN_FRAC         0xD9U
#define MAX35103_REG_HIT4DN_INT          0xDAU
#define MAX35103_REG_HIT4DN_FRAC         0xDBU
#define MAX35103_REG_HIT5DN_INT          0xDCU
#define MAX35103_REG_HIT5DN_FRAC         0xDDU
#define MAX35103_REG_HIT6DN_INT          0xDEU
#define MAX35103_REG_HIT6DN_FRAC         0xDFU

/* Averaged TOF result read opcodes. */
#define MAX35103_REG_AVGUP_INT           0xD1U
#define MAX35103_REG_AVGUP_FRAC          0xD2U
#define MAX35103_REG_AVGDN_INT           0xE0U
#define MAX35103_REG_AVGDN_FRAC          0xE1U
#define MAX35103_REG_TOF_DIFF_INT        0xE2U
#define MAX35103_REG_TOF_DIFF_FRAC       0xE3U
#define MAX35103_REG_CYCLE_COUNT         0xE4U
#define MAX35103_REG_TOF_DIFF_AVG_INT    0xE5U
#define MAX35103_REG_TOF_DIFF_AVG_FRAC   0xE6U

/* Direct temperature result read opcodes. */
#define MAX35103_REG_T1_INT              0xE7U
#define MAX35103_REG_T1_FRAC             0xE8U
#define MAX35103_REG_T2_INT              0xE9U
#define MAX35103_REG_T2_FRAC             0xEAU
#define MAX35103_REG_T3_INT              0xEBU
#define MAX35103_REG_T3_FRAC             0xECU
#define MAX35103_REG_T4_INT              0xEDU
#define MAX35103_REG_T4_FRAC             0xEEU

/* Event-timing temperature average and cycle-count read opcodes. */
#define MAX35103_REG_TEMP_CYCLE_COUNT    0xEFU
#define MAX35103_REG_T1_AVG_INT          0xF0U
#define MAX35103_REG_T1_AVG_FRAC         0xF1U
#define MAX35103_REG_T2_AVG_INT          0xF2U
#define MAX35103_REG_T2_AVG_FRAC         0xF3U
#define MAX35103_REG_T3_AVG_INT          0xF4U
#define MAX35103_REG_T3_AVG_FRAC         0xF5U
#define MAX35103_REG_T4_AVG_INT          0xF6U
#define MAX35103_REG_T4_AVG_FRAC         0xF7U

/* Status read opcode. */
#define MAX35103_REG_INT_STATUS          0xFEU
#define MAX35103_REG_CONTROL             0x7FU

/* Interrupt Status flags (register 0xFE, self-clearing on read). */
#define MAX35103_INT_TIMEOUT             0x8000U
#define MAX35103_INT_ALARM               0x4000U
#define MAX35103_INT_TOF_COMPLETE        0x1000U
#define MAX35103_INT_TEMP_COMPLETE       0x0800U
#define MAX35103_INT_LDO_READY           0x0400U
#define MAX35103_INT_TOF_EVTMG           0x0200U
#define MAX35103_INT_TEMP_EVTMG          0x0100U
#define MAX35103_INT_FLASH_READY         0x0080U
#define MAX35103_INT_CAL_COMPLETE        0x0040U
#define MAX35103_INT_HALT_COMPLETE       0x0020U
#define MAX35103_INT_CASE_SWITCH         0x0010U
#define MAX35103_INT_INIT_COMPLETE       0x0008U
#define MAX35103_INT_POR                 0x0004U

/* Calibration and Control register bits used by the driver. */
#define MAX35103_CAL_CTRL_CMP_EN         0x0800U
#define MAX35103_CAL_CTRL_CMP_SEL        0x0400U
#define MAX35103_CAL_CTRL_INT_EN         0x0200U
#define MAX35103_CAL_CTRL_ET_CONT        0x0100U
#define MAX35103_CAL_CTRL_CONT_INT       0x0080U

/* Configuration masks used for board-profile validation. */
#define MAX35103_TOF1_PL_MASK            0xFF00U
#define MAX35103_TOF1_PL_SHIFT                8U
#define MAX35103_TOF1_DPL_MASK           0x00F0U
#define MAX35103_TOF1_RESERVED_MASK      0x0004U
#define MAX35103_TOF1_STOP_POL_MASK      0x0008U
#define MAX35103_TOF1_CT_MASK            0x0003U
#define MAX35103_TOF2_STOP_MASK          0xE000U
#define MAX35103_TOF2_STOP_SHIFT             13U
#define MAX35103_TOF2_T2WV_MASK          0x1F80U
#define MAX35103_TOF2_T2WV_SHIFT              7U
#define MAX35103_TOF2_TIMEOUT_MASK       0x0007U
#define MAX35103_TOF2_RESERVED_MASK      0x0008U
#define MAX35103_TOF3_5_RESERVED_MASK    0xC0C0U
#define MAX35103_TOF_WAVE_SELECT_MASK    0x003FU
#define MAX35103_TOF6_7_RESERVED_MASK    0x0080U
#define MAX35103_CAL_CTRL_RESERVED_MASK  0xF000U
#define MAX35103_TOF_DELAY_MIN           0x0012U

/* Application-supported pulse launch and HIT ranges. */
#define MAX35103_PL_MIN                        1U
#define MAX35103_PL_MAX                      127U
#define MAX35103_STOP_CODE_MAX                 5U

/* Event Timing 2 temperature-port selection (TP[1:0], bits 6:5). */
#define MAX35103_EVT2_TEMP_PORT_MASK     0x0060U
#define MAX35103_EVT2_TEMP_T1_T3         0x0000U
#define MAX35103_EVT2_TEMP_T2_T4         0x0020U
#define MAX35103_EVT2_TEMP_T1_T3_T2      0x0040U
#define MAX35103_EVT2_TEMP_ALL           0x0060U

/* Bit masks used in Max35103TemperatureResult port fields. */
#define MAX35103_TEMP_PORT_T1            0x01U
#define MAX35103_TEMP_PORT_T2            0x02U
#define MAX35103_TEMP_PORT_T3            0x04U
#define MAX35103_TEMP_PORT_T4            0x08U

/**
 * @name Driver timing defaults
 *
 * All values are milliseconds. A zero timeout in Max35103Profile selects the
 * matching default below. RESET constants describe the active reset pulse and
 * the minimum settling delays used by MAX35103_ResetDevice().
 * @{
 */
#define MAX35103_INIT_TIMEOUT_MS          100U
#define MAX35103_RESULT_TIMEOUT_MS        200U
#define MAX35103_HALT_TIMEOUT_MS          100U
#define MAX35103_SPI_TIMEOUT_MS            10U
#define MAX35103_RESET_PULSE_MS             1U
#define MAX35103_RESET_READY_MS             1U
#define MAX35103_INIT_SETTLE_MS              3U
/** @} */

/**
 * @brief Status returned by the portable driver API.
 *
 * Negative values represent distinct failure or flow-control conditions.
 * MAX35103_BUSY and MAX35103_NO_RESULT are normally recoverable and do not by
 * themselves indicate a broken device.
 */
typedef enum {
    MAX35103_OK           = 0,   /**< Operation completed successfully. */
    MAX35103_BUSY         = -1,  /**< Another operation owns the driver/bus. */
    MAX35103_TIMEOUT      = -2,  /**< The operation exceeded its deadline. */
    MAX35103_INVALID_ARG  = -3,  /**< A pointer, opcode, size, or enum is invalid. */
    MAX35103_NOT_READY    = -4,  /**< Driver/device/configuration is not ready. */
    MAX35103_SPI_ERROR    = -5,  /**< Platform SPI transfer failed. */
    MAX35103_DEVICE_ERROR = -6,  /**< Device status or result evidence is invalid. */
    MAX35103_CONFIG_ERROR = -7,  /**< Register image violates a configuration gate. */
    MAX35103_NO_RESULT    = -8,  /**< The requested FIFO or result bank is empty. */
    MAX35103_STALE        = -9,  /**< Host profile no longer proves device contents. */
    MAX35103_OUT_OF_RANGE = -10, /**< Physical input is outside supported bounds. */
} Max35103Status;

/**
 * @brief Result of one platform transport operation.
 *
 * The platform adapter maps its native HAL/RTOS result into this smaller set so
 * that the core remains independent of STM32 HAL and any particular scheduler.
 */
typedef enum {
    MAX35103_TRANSPORT_OK = 0, /**< Transaction or resource operation succeeded. */
    MAX35103_TRANSPORT_BUSY,   /**< Transport resource is currently owned. */
    MAX35103_TRANSPORT_TIMEOUT,/**< Platform timeout expired. */
    MAX35103_TRANSPORT_ERROR,  /**< Non-timeout platform failure. */
} Max35103TransportStatus;

/** Queue-full policy. Every overflow is counted regardless of policy. */
typedef enum {
    /** Remove the FIFO head, then append the newly completed measurement. */
    MAX35103_QUEUE_DROP_OLDEST = 0,
    /** Keep every queued item and discard the newly completed measurement. */
    MAX35103_QUEUE_DROP_NEWEST,
} Max35103QueueOverflowPolicy;

/**
 * Platform operations required by the portable core.
 *
 * transfer() owns one complete NSS-low SPI transaction. rx may be NULL for an
 * execution command. lock()/unlock() optionally protect a shared SPI bus around
 * the complete transaction, including the full lifetime of an asynchronous
 * transfer. start_transfer_async() and cancel_transfer_async() are optional and
 * are used only by MAX35103_StartPendingSpiAsync(). set_reset() receives true
 * while the active-low hardware reset is asserted. The driver copies this
 * table, but context remains borrowed and must outlive the driver.
 */
typedef struct {
    /** Execute one complete blocking SPI transaction with NSS ownership. */
    Max35103TransportStatus (*transfer)(
        void *context, const uint8_t *tx, uint8_t *rx,
        uint16_t length, uint32_t timeout_ms);
    /** Assert or release the active-low hardware reset input. */
    Max35103TransportStatus (*set_reset)(
        void *context, bool asserted);
    /** Return a monotonically wrapping millisecond tick. */
    uint32_t (*get_tick_ms)(void *context);
    /** Delay the calling thread/task for at least delay_ms milliseconds. */
    void (*delay_ms)(void *context, uint32_t delay_ms);
    /** Borrowed platform object passed unchanged to every transport hook. */
    void *context;
    /** Start a complete asynchronous SPI transaction identified by token. */
    Max35103TransportStatus (*start_transfer_async)(
        void *context, const uint8_t *tx, uint8_t *rx,
        uint16_t length, uint32_t token);
    /** Abort the asynchronous transaction identified by token. */
    Max35103TransportStatus (*cancel_transfer_async)(
        void *context, uint32_t token);
    /** Optionally acquire the complete shared-SPI transaction lifetime. */
    Max35103TransportStatus (*lock)(
        void *context, uint32_t timeout_ms);
    /** Release the resource acquired by lock(). */
    void (*unlock)(void *context);
} Max35103Transport;

/**
 * @brief Observable state of the MAX35103 control and result-read FSM.
 *
 * EVENT_RUNNING is a stable waiting state and is not considered busy by
 * MAX35103_IsBusy(); the application may continue its normal main loop until
 * MAX_INT schedules DRAIN_STATUS.
 */
typedef enum {
    MAX35103_STATE_UNINIT = 0,       /**< No valid transport is installed. */
    MAX35103_STATE_IDLE,             /**< Ready for a blocking control command. */
    MAX35103_STATE_ARMING,           /**< Reset/config/start preparation in progress. */
    MAX35103_STATE_EVENT_RUNNING,    /**< Event engine active; waiting for MAX_INT. */
    MAX35103_STATE_DRAIN_STATUS,     /**< Deferred INT_STATUS read is pending/running. */
    MAX35103_STATE_READ_RESULT,      /**< Deferred TOF bank read is pending/running. */
    MAX35103_STATE_HALTING,          /**< HALT issued; waiting for completion flag. */
    MAX35103_STATE_SELF_CHECK,       /**< Direct TOF_DIFF self-check in progress. */
    MAX35103_STATE_TIMEOUT,          /**< Current deferred event exceeded its deadline. */
    MAX35103_STATE_ERROR,            /**< Unrecoverable operation/transport failure. */
    MAX35103_STATE_TEMP_MEASURING,   /**< Direct temperature command in progress. */
    MAX35103_STATE_READ_TEMP_RESULT, /**< Deferred temperature bank read in progress. */
} Max35103State;

/** Complete active configuration image for one product/sensor variant. */
typedef struct {
    /** Application-defined identity used to trace a calibrated register set. */
    uint32_t profile_id;
    /** Application-defined schema/revision number for profile persistence. */
    uint32_t profile_version;

    /** One of MAX35103_CMD_EVTMG1, MAX35103_CMD_EVTMG2, or MAX35103_CMD_EVTMG3. */
    uint8_t event_mode_cmd;

    /*
     * Complete volatile register image. MAX35103_Configure() writes every
     * member, including zero values, and verifies each register by readback.
     */
    uint16_t tof1;
    uint16_t tof2;
    uint16_t tof3;
    uint16_t tof4;
    uint16_t tof5;
    uint16_t tof6;
    uint16_t tof7;
    uint16_t event_timing_1;
    uint16_t event_timing_2;
    uint16_t tof_measurement_delay;
    uint16_t calibration_control;

    uint32_t init_timeout_ms;   /**< INIT deadline in ms; zero selects default. */
    uint32_t result_timeout_ms; /**< Deferred result deadline in ms; zero selects default. */
    uint32_t halt_timeout_ms;   /**< HALT completion deadline in ms; zero selects default. */

    /*
     * Host-side temperature conversion data. Set either value to zero to
     * return raw T1..T4 timings without resistance/temperature conversion.
     * The standard System Diagram connects T1/T2 to platinum RTDs and T3/T4
     * to the same reference resistor.
     */
    uint32_t reference_resistance_milliohm; /**< Board reference resistor, in mOhm. */
    uint32_t rtd_nominal_resistance_milliohm; /**< RTD R0 at 0 C, in mOhm. */
} Max35103Profile;

/**
 * Raw register evidence plus nominal-time conversion.
 *
 * The ps fields assume an exact 4 MHz clock. Calibration/gain correction must
 * be applied by the measurement-processing layer when required. tof_diff_*
 * always contains the direct E2/E3 value and tof_diff_avg_* contains E5/E6.
 * selected_tof_diff_* selects the direct value for TOF_COMPLETE and the
 * hardware-averaged value for TOF_EVTMG.
 */
typedef struct {
    /* Raw 16-bit words from AVGUP, AVGDN, TOF_DIFF, cycle/range, and AVGDIFF. */
    uint16_t avg_up_int;
    uint16_t avg_up_frac;
    uint16_t avg_down_int;
    uint16_t avg_down_frac;
    uint16_t tof_diff_int;
    uint16_t tof_diff_frac;
    uint16_t cycle_range_word;
    uint16_t tof_diff_avg_int;
    uint16_t tof_diff_avg_frac;

    /* Reconstructed Q16 values. Unsigned UP/DOWN and signed differences. */
    uint32_t tof_up_q16;
    uint32_t tof_down_q16;
    int32_t  tof_diff_q16;
    int32_t  tof_diff_avg_q16;
    int32_t  selected_tof_diff_q16;

    /*
     * Nominal conversions in picoseconds, assuming an exact 4 MHz reference.
     * These are convenience values and are not oscillator-calibrated results.
     */
    int64_t  tof_up_ps;
    int64_t  tof_down_ps;
    int64_t  tof_diff_ps;
    int64_t  tof_diff_avg_ps;
    int64_t  selected_tof_diff_ps;

    uint8_t  valid_cycle_count; /**< EVTMG cycles included in hardware average. */
    uint8_t  tof_range;         /**< Raw range byte from the cycle/range word. */
    uint16_t status_flags;      /**< Snapshot of self-clearing INT_STATUS. */
    uint64_t timestamp_us;      /**< Host MAX_INT timestamp, in microseconds. */
    uint32_t sequence_number;   /**< Nonzero host event identity. */
    bool     selected_tof_diff_is_average; /**< true selects E5/E6, false E2/E3. */
    bool     valid;             /**< All status, sentinel, and coherence gates passed. */
} Max35103RawResult;

/**
 * Per-wave evidence produced by the most recent TOF_UP/TOF_DOWN pair.
 *
 * WVR bytes are unsigned Q1.7 ratios: the high byte is t1/t2 and the low
 * byte is t2/tideal. Hit times are unsigned Q16 counts of the 4 MHz clock.
 * Only configured_hit_count entries are populated. This snapshot is intended
 * for characterization and auto-tuning; normal production measurement can
 * continue to consume Max35103RawResult.
 */
typedef struct {
    /** Raw upstream and downstream wave-ratio registers. */
    uint16_t wvr_up;
    uint16_t wvr_down;
    /** Q1.7 ratios extracted from the two bytes of each WVR register. */
    uint8_t wvr_up_t1_t2_q7;
    uint8_t wvr_up_t2_ideal_q7;
    uint8_t wvr_down_t1_t2_q7;
    uint8_t wvr_down_t2_ideal_q7;

    /** Raw integer/fraction words for each configured upstream/downstream HIT. */
    uint16_t hit_up_int[MAX35103_WAVE_HIT_COUNT];
    uint16_t hit_up_frac[MAX35103_WAVE_HIT_COUNT];
    uint16_t hit_down_int[MAX35103_WAVE_HIT_COUNT];
    uint16_t hit_down_frac[MAX35103_WAVE_HIT_COUNT];
    /** Reconstructed unsigned Q16 hit times. */
    uint32_t hit_up_q16[MAX35103_WAVE_HIT_COUNT];
    uint32_t hit_down_q16[MAX35103_WAVE_HIT_COUNT];
    /** Nominal hit times in picoseconds at a 4 MHz reference clock. */
    int64_t hit_up_ps[MAX35103_WAVE_HIT_COUNT];
    int64_t hit_down_ps[MAX35103_WAVE_HIT_COUNT];

    uint32_t avg_up_q16;  /**< Hardware AVGUP reconstructed as unsigned Q16. */
    uint32_t avg_down_q16;/**< Hardware AVGDN reconstructed as unsigned Q16. */
    uint8_t configured_hit_count; /**< Number of populated HIT slots, 1..6. */
    bool avg_up_consistent;   /**< AVGUP equals rounded mean of upstream HITs. */
    bool avg_down_consistent; /**< AVGDN equals rounded mean of downstream HITs. */
    bool valid; /**< WVR, HIT ordering, sentinels, and both averages are coherent. */
} Max35103WaveEvidence;

/**
 * Temperature timing evidence and optional platinum-RTD conversion.
 *
 * Each port value is an unsigned Q16 count of 4 MHz clock periods. With the
 * standard connection, RRTD1/RREF = T1/T3 and RRTD2/RREF = T2/T4.
 */
typedef struct {
    /** Raw integer and fraction words for T1, T2, T3, and T4. */
    uint16_t port_int[MAX35103_TEMP_PORT_COUNT];
    uint16_t port_frac[MAX35103_TEMP_PORT_COUNT];
    /** Reconstructed unsigned Q16 timing for each temperature port. */
    uint32_t port_q16[MAX35103_TEMP_PORT_COUNT];

    uint32_t rtd1_resistance_milliohm; /**< T1/T3 derived resistance, in mOhm. */
    uint32_t rtd2_resistance_milliohm; /**< T2/T4 derived resistance, in mOhm. */
    int32_t rtd1_temperature_millicelsius; /**< IEC 60751 result, in mC. */
    int32_t rtd2_temperature_millicelsius; /**< IEC 60751 result, in mC. */

    uint16_t status_flags;   /**< Snapshot of self-clearing INT_STATUS. */
    uint64_t timestamp_us;   /**< Host MAX_INT timestamp, in microseconds. */
    uint32_t sequence_number;/**< Nonzero host event identity. */
    uint8_t selected_port_mask; /**< Ports requested by EVT_TIMING_2 TP bits. */
    uint8_t valid_port_mask;    /**< Selected ports with usable timing values. */
    uint8_t short_circuit_mask; /**< Selected ports that decoded as zero. */
    uint8_t open_circuit_mask;  /**< Selected ports with invalid/sentinel timing. */
    uint8_t valid_cycle_count;  /**< EVTMG temperature cycles in the average. */
    bool averaged;              /**< true for event-timing average registers. */
    bool rtd1_valid;            /**< T1/T3 ratio produced a resistance. */
    bool rtd2_valid;            /**< T2/T4 ratio produced a resistance. */
    bool rtd1_temperature_valid;/**< RTD1 lies in IEC 60751 conversion range. */
    bool rtd2_temperature_valid;/**< RTD2 lies in IEC 60751 conversion range. */
    bool valid;                 /**< Every selected timing port passed validation. */
} Max35103TemperatureResult;

/** Borrowed view of a pending caller-driven SPI transaction. */
typedef struct {
    const uint8_t *tx; /**< Driver-owned transmit frame; valid until completion. */
    uint8_t *rx;       /**< Driver-owned receive frame; valid until completion. */
    uint16_t length;   /**< Total transaction length, in bytes. */
    uint32_t token;    /**< Nonzero generation token required by OnSpiDone(). */
} Max35103SpiRequest;

/**
 * @brief Complete mutable state for one MAX35103 instance.
 *
 * The application allocates this object, but its members are implementation
 * state and diagnostic counters. Modify it only through the public API. Unless
 * external serialization is provided, exactly one worker context must own the
 * object and its result queues.
 */
typedef struct {
    /* Injected platform services and high-level state-machine generation. */
    Max35103Transport transport;
    Max35103State state;
    uint32_t generation;
    /** Operation start/deadline in the caller's microsecond time domain. */
    uint64_t attempt_start_us;
    uint64_t deadline_us;

    /*
     * Driver-owned copy of the register image that was last verified against
     * the device. profile_synchronized becomes false after reset, POR, an
     * unverified configuration write, or a write that cannot be represented
     * by Max35103Profile.
     */
    Max35103Profile active_profile;
    bool profile_synchronized;
    bool device_ready;
    bool configured;
    bool event_timing_active;
    bool irq_recheck_pending;

    /*
     * Driver-owned SPI staging. spi_pending means a frame awaits execution;
     * spi_async_active means the platform owns that frame until completion.
     * spi_bus_locked spans the entire asynchronous lifetime.
     */
    uint8_t tx_buf[MAX35103_MAX_SPI_FRAME_BYTES];
    uint8_t rx_buf[MAX35103_MAX_SPI_FRAME_BYTES];
    uint16_t spi_length;
    uint32_t spi_token;
    uint32_t next_spi_token;
    bool spi_pending;
    bool spi_async_active;
    bool spi_bus_locked;

    /* Raw result staging and one-read INT_STATUS snapshot for the active event. */
    uint8_t result_frame[MAX35103_TOF_RESULT_BANK_DATA_BYTES];
    uint8_t temperature_frame[MAX35103_TEMP_RESULT_FRAME_BYTES];
    uint8_t result_word_index;
    uint16_t temperature_cycle_word;
    uint16_t latched_status;
    uint16_t expected_event_flags;
    uint16_t seen_event_flags;
    uint64_t interrupt_timestamp_us;
    uint64_t pending_irq_timestamp_us;

    /* Bounded FIFO storage. head points to the oldest unread entry. */
    Max35103RawResult result_queue[MAX35103_RESULT_QUEUE_CAPACITY];
    Max35103TemperatureResult
        temperature_queue[MAX35103_TEMPERATURE_QUEUE_CAPACITY];
    uint8_t result_queue_head;
    uint8_t result_queue_count;
    uint8_t temperature_queue_head;
    uint8_t temperature_queue_count;
    Max35103QueueOverflowPolicy queue_overflow_policy;

    /*
     * A single EVTMG1 cycle publishes TOF and temperature with the same active
     * sequence number. Zero is reserved for "not yet assigned".
     */
    uint32_t next_sequence_number;
    uint32_t active_event_sequence_number;
    bool active_event_sequence_valid;

    /* Monotonic diagnostics; cleared only when the driver is reinitialized. */
    uint32_t irq_count;
    uint32_t irq_recheck_count;
    uint32_t unexpected_irq_count;
    uint32_t spi_done_count;
    uint32_t stale_spi_completion_count;
    uint32_t timeout_count;
    uint32_t error_count;
    uint32_t result_count;
    uint32_t invalid_result_count;
    uint32_t dropped_result_count;
    uint32_t result_queue_overflow_count;
    uint32_t temperature_result_count;
    uint32_t invalid_temperature_result_count;
    uint32_t dropped_temperature_result_count;
    uint32_t temperature_queue_overflow_count;
    uint32_t bus_lock_failure_count;
    uint32_t dma_start_count;
    uint32_t dma_cancel_count;
    uint32_t dma_error_count;
} Max35103Driver;

/**
 * @brief Initialize one driver instance with caller-owned platform operations.
 *
 * transfer(), get_tick_ms(), and delay_ms() are mandatory. set_reset() may be
 * NULL only when MAX35103_ResetDevice() is not used. The transport table is
 * copied, but transport->context and all resources reachable from it remain
 * borrowed. Initialization does not communicate with the IC.
 *
 * @param[out] drv Driver object to clear and initialize.
 * @param[in] transport Fully configured platform-operation table.
 *
 * @retval MAX35103_OK Driver entered MAX35103_STATE_IDLE.
 * @retval MAX35103_INVALID_ARG drv is NULL or the transport hooks are invalid.
 *
 * @pre Platform SPI/GPIO/tick services are initialized.
 * @post Result FIFOs and diagnostic counters are cleared.
 */
Max35103Status MAX35103_Init(
    Max35103Driver *drv, const Max35103Transport *transport);

/**
 * @brief Pulse hardware RST, verify POR, issue INIT, and wait for completion.
 *
 * This restores the device flash image; call Configure() afterwards to apply
 * the production profile without writing flash. INT_STATUS is read during this
 * blocking sequence and therefore no event-timing operation may be active.
 *
 * @param[in,out] drv Initialized driver instance.
 *
 * @retval MAX35103_OK POR and INIT_COMPLETE were observed.
 * @retval MAX35103_INVALID_ARG drv is NULL.
 * @retval MAX35103_NOT_READY The driver is uninitialized or reset control is absent.
 * @retval MAX35103_SPI_ERROR Reset GPIO or SPI access failed.
 * @retval MAX35103_DEVICE_ERROR POR was absent or the SPI bus returned 0xFFFF.
 * @retval MAX35103_TIMEOUT INIT_COMPLETE was not observed before the deadline.
 *
 * @post A successful reset sets device_ready=true but invalidates the active
 *       profile; MAX35103_Configure() is required before event timing.
 */
Max35103Status MAX35103_ResetDevice(Max35103Driver *drv);

/**
 * @brief Validate a complete register image without accessing the device.
 *
 * This rejects PL outside the application-supported range 1..127, STOP codes
 * above 5 (the device exposes at most six HIT results), disabled DPL, reserved
 * bits, an unsupported measurement delay, and a delay longer than the TOF2
 * timeout. It checks structural safety only; transducer-specific wave and
 * comparator settings still require board-level characterization.
 *
 * @param[in] profile Candidate volatile-register image.
 *
 * @retval MAX35103_OK All structural configuration gates passed.
 * @retval MAX35103_INVALID_ARG profile is NULL.
 * @retval MAX35103_CONFIG_ERROR At least one field, reserved bit, timeout,
 *         delay, or effective wave sequence is invalid.
 *
 * @note Passing this function proves internal consistency, not acoustic
 *       suitability for a particular transducer, pipe, or installation.
 */
Max35103Status MAX35103_ValidateProfile(const Max35103Profile *profile);

/**
 * @brief Apply and read-verify the complete volatile configuration image.
 *
 * The caller's profile is copied into driver-owned storage only after every
 * configuration register has passed readback verification. The command never
 * writes the MAX35103 flash.
 *
 * @param[in,out] drv Ready, idle driver instance.
 * @param[in] profile Complete profile to validate, write, and verify.
 *
 * @retval MAX35103_OK Every register read back exactly and the profile shadow
 *         is synchronized.
 * @retval MAX35103_INVALID_ARG A pointer is NULL.
 * @retval MAX35103_NOT_READY Hardware reset/INIT has not completed.
 * @retval MAX35103_BUSY An event, deferred SPI operation, or control operation
 *         currently owns the device.
 * @retval MAX35103_CONFIG_ERROR The profile is structurally invalid or a
 *         register readback differs from the requested value.
 * @retval MAX35103_SPI_ERROR A write or readback transaction failed.
 */
Max35103Status MAX35103_Configure(Max35103Driver *drv, const Max35103Profile *profile);

/**
 * @brief Start the configured EVTMG1, EVTMG2, or EVTMG3 command.
 *
 * @param[in,out] drv Ready driver with a synchronized active profile.
 *
 * @retval MAX35103_OK The command was accepted and state is EVENT_RUNNING.
 * @retval MAX35103_INVALID_ARG drv is NULL.
 * @retval MAX35103_NOT_READY Device/profile is not ready or synchronized.
 * @retval MAX35103_BUSY Another operation or event timing is active.
 * @retval MAX35103_CONFIG_ERROR The profile's event opcode is unsupported.
 * @retval MAX35103_SPI_ERROR The command transaction failed.
 *
 * @note Completion is asynchronous. Forward MAX_INT to MAX35103_OnInt() and
 *       call MAX35103_Process() until the result appears in the FIFO.
 */
Max35103Status MAX35103_StartEventTiming(Max35103Driver *drv);

/**
 * @brief Send HALT and block until HALT_COMPLETE is observed.
 *
 * @param[in,out] drv Initialized driver instance.
 *
 * @retval MAX35103_OK The event engine stopped and the driver returned idle.
 * @retval MAX35103_INVALID_ARG drv is NULL.
 * @retval MAX35103_NOT_READY The driver/device is not initialized.
 * @retval MAX35103_SPI_ERROR Command or status polling failed.
 * @retval MAX35103_TIMEOUT HALT_COMPLETE was not observed before the deadline.
 *
 * @post Pending deferred SPI work is cancelled and event_timing_active is false.
 */
Max35103Status MAX35103_Halt(Max35103Driver *drv);

/**
 * @brief Execute one blocking direct TOF_DIFF self-check measurement.
 *
 * On completion, the result is put
 * in the normal FIFO and can be taken with GetResult()/ResultPop().
 *
 * @param[in,out] drv Ready, configured, idle driver instance.
 *
 * @retval MAX35103_OK A valid direct result was decoded and queued.
 * @retval MAX35103_INVALID_ARG drv is NULL.
 * @retval MAX35103_NOT_READY Device/profile is not ready.
 * @retval MAX35103_BUSY Event timing or another operation is active.
 * @retval MAX35103_SPI_ERROR Command/status/result SPI access failed.
 * @retval MAX35103_DEVICE_ERROR Device status or result validation failed.
 * @retval MAX35103_TIMEOUT Direct measurement did not complete in time.
 */
Max35103Status MAX35103_SelfCheck(Max35103Driver *drv);

/**
 * @brief Execute one direct, blocking TEMPERATURE command and read T1..T4.
 *
 * Event Timing 2 selects which ports are measured. The profile's reference
 * and nominal RTD resistance values enable resistance and IEC 60751 platinum
 * RTD conversion; raw timing remains available when those values are zero.
 *
 * @param[in,out] drv Ready, configured, idle driver instance.
 * @param[out] result Decoded timing, resistance, and optional temperature data.
 *
 * @retval MAX35103_OK Selected timing ports passed validation.
 * @retval MAX35103_INVALID_ARG A pointer is NULL.
 * @retval MAX35103_NOT_READY Device/profile is not ready.
 * @retval MAX35103_BUSY Event timing or another operation is active.
 * @retval MAX35103_SPI_ERROR Command/status/result SPI access failed.
 * @retval MAX35103_DEVICE_ERROR Selected timing evidence is invalid.
 * @retval MAX35103_TIMEOUT Temperature conversion did not complete in time.
 */
Max35103Status MAX35103_MeasureTemperature(
    Max35103Driver *drv, Max35103TemperatureResult *result);

/**
 * @brief Cancel host-side deferred work and discard unread queue entries.
 *
 * The device event engine is not halted; call Halt() when hardware must stop.
 *
 * @param[in,out] drv Driver instance; NULL is accepted as a no-op.
 *
 * @post Any active async transfer is cancelled, the shared-bus lock is
 *       released, and both result queues are empty.
 */
void MAX35103_Cancel(Max35103Driver *drv);

/**
 * @brief Record falling-edge MAX_INT evidence and schedule INT_STATUS draining.
 *
 * An edge received while the result FSM is busy is retained and causes
 * INT_STATUS to be drained again after the current snapshot completes.
 *
 * @param[in,out] drv Driver instance whose event FSM owns MAX_INT.
 * @param[in] now_us Timestamp of the observed edge, in microseconds.
 *
 * @note The function only prepares driver state and SPI buffers; it does not
 *       call the blocking transport. In an RTOS design, invoke it in the same
 *       deferred worker that owns MAX35103_Process().
 * @warning Do not concurrently call this function and queue/FSM APIs without
 *          an external critical-section policy.
 */
void MAX35103_OnInt(Max35103Driver *drv, uint64_t now_us);

/**
 * @brief Obtain a borrowed view of the pending deferred SPI transaction.
 *
 * @param[in] drv Driver instance.
 * @param[out] request Driver-owned buffers, frame length, and completion token.
 *
 * @return true when one unstarted SPI request is pending.
 * @return false when no request is pending or an argument is invalid.
 *
 * @warning request->tx and request->rx remain owned by drv. Do not retain them
 *          after completion, cancellation, reinitialization, or another request.
 */
bool MAX35103_GetPendingSpiRequest(Max35103Driver *drv,
                                   Max35103SpiRequest *request);

/**
 * Start the pending request through transport.start_transfer_async().
 *
 * The core acquires the optional shared-bus lock before starting DMA and keeps
 * it until MAX35103_OnSpiDone(), timeout, cancellation, or an error releases
 * the transaction. Returns MAX35103_NOT_READY when no async hook is installed.
 *
 * @param[in,out] drv Driver with one pending, not-yet-started SPI request.
 *
 * @retval MAX35103_OK Platform DMA/asynchronous transfer started.
 * @retval MAX35103_INVALID_ARG drv is NULL.
 * @retval MAX35103_NOT_READY Async hooks are absent or no request is pending.
 * @retval MAX35103_BUSY A transfer is already active or the bus lock is busy.
 * @retval MAX35103_TIMEOUT The platform bus lock timed out.
 * @retval MAX35103_SPI_ERROR The async start hook failed.
 */
Max35103Status MAX35103_StartPendingSpiAsync(Max35103Driver *drv);

/**
 * Complete an externally executed transaction. token must match the request
 * returned by GetPendingSpiRequest(); stale completions are ignored. An STM32
 * DMA adapter must deassert NSS before calling this function.
 *
 * @param[in,out] drv Driver that owns the completed request.
 * @param[in] token Nonzero token captured when the transfer was started.
 * @param[in] transfer_ok true when every byte completed successfully.
 *
 * @post A matching completion releases the shared-bus lock and advances the
 *       result FSM. A stale token only increments stale_spi_completion_count.
 * @warning Call in the serialized worker context unless the platform provides
 *          explicit synchronization around every field accessed here.
 */
void MAX35103_OnSpiDone(Max35103Driver *drv, uint32_t token,
                        bool transfer_ok);

/**
 * @brief Execute one pending deferred transaction through blocking transport.
 *
 * @param[in,out] drv Driver with one pending SPI request.
 *
 * @retval MAX35103_OK Transfer completed and the FSM consumed its response.
 * @retval MAX35103_INVALID_ARG drv is NULL.
 * @retval MAX35103_NOT_READY No request is pending.
 * @retval MAX35103_BUSY An async transfer already owns the request/bus.
 * @retval MAX35103_TIMEOUT Platform transfer timed out.
 * @retval MAX35103_SPI_ERROR Platform transfer failed.
 */
Max35103Status MAX35103_ExecuteSpi(Max35103Driver *drv);

/**
 * Check the deadline and advance at most one pending SPI transaction.
 *
 * When start_transfer_async() is installed, Process() starts DMA and returns
 * MAX35103_BUSY until the platform completion callback calls OnSpiDone().
 * Otherwise the existing blocking transfer path is used.
 *
 * @param[in,out] drv Driver/FSM instance.
 * @param[in] now_us Current monotonic time, in microseconds.
 *
 * @retval MAX35103_OK No work is pending or one blocking step completed.
 * @retval MAX35103_BUSY Async work is active or has just been started.
 * @retval MAX35103_TIMEOUT The deferred result deadline expired.
 * @retval MAX35103_DEVICE_ERROR Driver is already in the error state.
 * @retval MAX35103_SPI_ERROR Starting/executing the transport failed.
 * @retval MAX35103_INVALID_ARG drv is NULL.
 *
 * @note Call frequently enough that result_timeout_ms can be enforced with
 *       the required latency.
 */
Max35103Status MAX35103_Process(Max35103Driver *drv, uint64_t now_us);

/**
 * @brief Force timeout handling for the current deferred result-read operation.
 *
 * @param[in,out] drv Driver instance; NULL or a non-deferred state is a no-op.
 *
 * @post Missing expected event results are represented by invalid status-only
 *       FIFO entries and state becomes MAX35103_STATE_TIMEOUT.
 */
void MAX35103_OnTimeout(Max35103Driver *drv);

/**
 * @brief Read one 16-bit register using a blocking SPI transaction.
 *
 * Blocking register access is intended for initialization, diagnostics, and
 * HIL.
 * INT_STATUS is owned by the event FSM while event timing is active; attempts
 * to consume it through these public diagnostic paths return MAX35103_BUSY.
 *
 * @param[in,out] drv Ready driver instance.
 * @param[in] read_opcode Valid MAX35103 read opcode.
 * @param[out] value Register value reconstructed MSB first.
 *
 * @retval MAX35103_OK Register was read.
 * @retval MAX35103_INVALID_ARG Pointer/opcode is invalid.
 * @retval MAX35103_NOT_READY Device initialization has not completed.
 * @retval MAX35103_BUSY Deferred SPI or INT_STATUS ownership prevents the read.
 * @retval MAX35103_SPI_ERROR Transport failed.
 */
Max35103Status MAX35103_ReadReg(Max35103Driver *drv,
                                uint8_t read_opcode, uint16_t *value);
/**
 * @brief Read consecutive 16-bit registers in one NSS-low SPI transaction.
 *
 * start_read_opcode is sent once, followed by 2 * word_count dummy bytes.
 * The function accepts at most MAX35103_MAX_BLOCK_WORDS and never crosses
 * MAX35103_REG_INT_STATUS. CONTROL (0x7F) is valid only for one word.
 *
 * @param[in,out] drv Ready driver instance.
 * @param[in] start_read_opcode First sequential-read opcode.
 * @param[out] words Caller array with room for word_count 16-bit values.
 * @param[in] word_count Number of consecutive registers, in 16-bit words.
 *
 * @retval MAX35103_OK All words were read and decoded MSB first.
 * @retval MAX35103_INVALID_ARG Pointer, opcode range, or count is invalid.
 * @retval MAX35103_NOT_READY Device initialization has not completed.
 * @retval MAX35103_BUSY The FSM owns SPI or the read would consume INT_STATUS.
 * @retval MAX35103_SPI_ERROR Transport failed.
 */
Max35103Status MAX35103_ReadBlock(
    Max35103Driver *drv, uint8_t start_read_opcode,
    uint16_t *words, uint8_t word_count);

/**
 * @brief Write one 16-bit register without readback verification.
 *
 * A successful unverified write to a configuration register invalidates the
 * active-profile shadow. WriteVerifyReg() keeps the shadow synchronized when
 * the opcode maps to a profile field and the resulting complete profile is
 * valid; otherwise configuration-dependent operations remain blocked until a
 * successful Configure().
 *
 * @param[in,out] drv Ready, idle driver instance.
 * @param[in] write_opcode Valid configuration/control write opcode.
 * @param[in] value 16-bit register value.
 *
 * @retval MAX35103_OK SPI write completed.
 * @retval MAX35103_INVALID_ARG Pointer/opcode is invalid.
 * @retval MAX35103_NOT_READY Device initialization has not completed.
 * @retval MAX35103_BUSY Driver is not idle or event timing is active.
 * @retval MAX35103_CONFIG_ERROR Direct PL/STOP or candidate profile is invalid.
 * @retval MAX35103_SPI_ERROR Transport failed.
 */
Max35103Status MAX35103_WriteReg(Max35103Driver *drv,
                                 uint8_t write_opcode, uint16_t value);

/**
 * @brief Write one 16-bit register and require exact readback.
 *
 * If the driver previously held a synchronized profile and the opcode maps to
 * a profile member, the verified value is committed to a validated profile
 * copy. Other configuration writes deliberately invalidate profile ownership.
 *
 * @param[in,out] drv Ready, idle driver instance.
 * @param[in] write_opcode Valid configuration/control write opcode.
 * @param[in] value 16-bit register value.
 *
 * @retval MAX35103_OK Write and exact readback succeeded.
 * @retval MAX35103_INVALID_ARG Pointer/opcode is invalid.
 * @retval MAX35103_NOT_READY Device initialization has not completed.
 * @retval MAX35103_BUSY Driver is not idle or event timing is active.
 * @retval MAX35103_CONFIG_ERROR Value/profile is invalid or readback differs.
 * @retval MAX35103_SPI_ERROR Write or readback transfer failed.
 */
Max35103Status MAX35103_WriteVerifyReg(Max35103Driver *drv,
                                       uint8_t write_opcode, uint16_t value);

/**
 * @brief Test whether the driver-owned profile matches active IC registers.
 * @param[in] drv Driver instance.
 * @return true only after complete verified configuration or a safe verified
 *         field update; false for NULL, reset, POR, or unverified writes.
 */
bool MAX35103_IsProfileSynchronized(const Max35103Driver *drv);

/**
 * @brief Copy the synchronized active profile into caller-owned storage.
 * @param[in] drv Driver instance.
 * @param[out] profile Destination for the complete profile copy.
 * @retval MAX35103_OK A synchronized profile was copied.
 * @retval MAX35103_INVALID_ARG A pointer is NULL.
 * @retval MAX35103_NOT_READY Device initialization has not completed.
 * @retval MAX35103_STALE The host cannot prove the active register image.
 */
Max35103Status MAX35103_GetActiveProfile(
    const Max35103Driver *drv, Max35103Profile *profile);

/**
 * @brief Test whether at least one unread TOF result is queued.
 * @param[in] drv Driver instance.
 * @return true when the TOF FIFO is nonempty; false for NULL or an empty FIFO.
 */
bool MAX35103_HasResult(const Max35103Driver *drv);

/**
 * @brief Pop the oldest unread TOF result.
 *
 * Compatibility alias of MAX35103_ResultPop().
 *
 * @param[in,out] drv Driver instance and TOF FIFO owner.
 * @param[out] result Destination for the oldest queued result.
 * @retval MAX35103_OK One result was removed and copied.
 * @retval MAX35103_INVALID_ARG A pointer is NULL.
 * @retval MAX35103_NO_RESULT The TOF FIFO is empty.
 */
Max35103Status MAX35103_GetResult(Max35103Driver *drv,
                                  Max35103RawResult *result);

/**
 * @brief Return the number of unread TOF results.
 * @param[in] drv Driver instance.
 * @return FIFO occupancy in entries; zero for NULL.
 */
size_t MAX35103_ResultAvailable(const Max35103Driver *drv);

/**
 * @brief Remove and copy the oldest unread TOF result.
 * @param[in,out] drv Driver instance and TOF FIFO owner.
 * @param[out] result Destination for the removed result.
 * @retval MAX35103_OK One result was removed.
 * @retval MAX35103_INVALID_ARG A pointer is NULL.
 * @retval MAX35103_NO_RESULT FIFO is empty.
 */
Max35103Status MAX35103_ResultPop(
    Max35103Driver *drv, Max35103RawResult *result);

/**
 * Blocking direct-result read. Reads Interrupt Status exactly once and is
 * rejected while the event-timing FSM owns interrupt status.
 *
 * @param[in,out] drv Ready, idle driver instance.
 * @param[out] result Direct or event-average TOF result selected by status.
 *
 * @retval MAX35103_OK Result evidence passed validation.
 * @retval MAX35103_INVALID_ARG A pointer is NULL.
 * @retval MAX35103_NOT_READY Device initialization has not completed.
 * @retval MAX35103_BUSY Event/FSM/SPI ownership prevents the read.
 * @retval MAX35103_NO_RESULT No TOF completion flag is present.
 * @retval MAX35103_SPI_ERROR Status or result-bank SPI access failed.
 * @retval MAX35103_DEVICE_ERROR Error flags/sentinels/coherence gates failed.
 */
Max35103Status MAX35103_ReadResult(Max35103Driver *drv,
                                   Max35103RawResult *result);

/**
 * Read WVRUP/WVRDN and every configured HITx pair from the latest TOF result.
 *
 * This function does not read INT_STATUS and therefore does not consume
 * interrupt evidence. Call it after a successful TOF measurement, while the
 * driver is idle. It is intentionally blocking because auto-calibration runs
 * outside ISR context.
 *
 * @param[in,out] drv Ready, configured, idle driver instance.
 * @param[out] evidence WVR, HIT, average, and consistency snapshot.
 *
 * @retval MAX35103_OK Every evidence gate passed.
 * @retval MAX35103_INVALID_ARG A pointer is NULL.
 * @retval MAX35103_NOT_READY Device/profile is not ready.
 * @retval MAX35103_BUSY Driver or event timing is active.
 * @retval MAX35103_CONFIG_ERROR Configured STOP/HIT count is invalid.
 * @retval MAX35103_SPI_ERROR Sequential result-bank transfer failed.
 * @retval MAX35103_DEVICE_ERROR A sentinel, order, WVR, or average gate failed.
 */
Max35103Status MAX35103_ReadWaveEvidence(
    Max35103Driver *drv, Max35103WaveEvidence *evidence);

/**
 * @brief Decode the configured HIT count from the TOF2 STOP field.
 * @param[in] profile Profile containing the encoded STOP value.
 * @return Number of HITs in the inclusive range 1..6, or zero for NULL/invalid.
 */
uint8_t MAX35103_ConfiguredHitCount(const Max35103Profile *profile);

/**
 * @brief Test whether at least one unread temperature result is queued.
 * @param[in] drv Driver instance.
 * @return true when the temperature FIFO is nonempty; false otherwise.
 */
bool MAX35103_HasTemperatureResult(const Max35103Driver *drv);

/**
 * @brief Pop the oldest unread temperature result.
 *
 * Compatibility alias of MAX35103_TemperatureResultPop().
 *
 * @param[in,out] drv Driver instance and temperature FIFO owner.
 * @param[out] result Destination for the oldest queued result.
 * @retval MAX35103_OK One result was removed and copied.
 * @retval MAX35103_INVALID_ARG A pointer is NULL.
 * @retval MAX35103_NO_RESULT The temperature FIFO is empty.
 */
Max35103Status MAX35103_GetTemperatureResult(
    Max35103Driver *drv, Max35103TemperatureResult *result);

/**
 * @brief Return the number of unread temperature results.
 * @param[in] drv Driver instance.
 * @return FIFO occupancy in entries; zero for NULL.
 */
size_t MAX35103_TemperatureResultAvailable(const Max35103Driver *drv);

/**
 * @brief Remove and copy the oldest unread temperature result.
 * @param[in,out] drv Driver instance and temperature FIFO owner.
 * @param[out] result Destination for the removed result.
 * @retval MAX35103_OK One result was removed.
 * @retval MAX35103_INVALID_ARG A pointer is NULL.
 * @retval MAX35103_NO_RESULT FIFO is empty.
 */
Max35103Status MAX35103_TemperatureResultPop(
    Max35103Driver *drv, Max35103TemperatureResult *result);

/**
 * Select how full queues are handled. DROP_OLDEST is the initialization
 * default and preserves the freshest telemetry. DROP_NEWEST preserves every
 * queued sample until the application consumes it.
 *
 * @param[in,out] drv Driver instance.
 * @param[in] policy Requested policy for both TOF and temperature FIFOs.
 * @retval MAX35103_OK Policy was stored.
 * @retval MAX35103_INVALID_ARG drv or policy is invalid.
 *
 * @note Existing queue contents are not modified.
 */
Max35103Status MAX35103_SetQueueOverflowPolicy(
    Max35103Driver *drv, Max35103QueueOverflowPolicy policy);

/**
 * @brief Discard all unread TOF and temperature results.
 * @param[in,out] drv Driver instance; NULL is accepted as a no-op.
 * @note Lifetime diagnostic counters are intentionally preserved.
 */
void MAX35103_ClearResultQueues(Max35103Driver *drv);

/**
 * Convert a resistance from a platinum RTD with IEC 60751 alpha=0.00385.
 * Supports the standard -200 C to +850 C range and either PT100 or PT1000
 * through the caller-provided R0 value.
 *
 * @param[in] resistance_milliohm Measured RTD resistance, in milliohms.
 * @param[in] r0_milliohm Nominal resistance at 0 C, in milliohms.
 * @param[out] temperature_millicelsius Nearest temperature, in 0.001 C.
 *
 * @retval MAX35103_OK Conversion succeeded.
 * @retval MAX35103_INVALID_ARG Output is NULL or R0 is zero.
 * @retval MAX35103_OUT_OF_RANGE Resistance lies outside the IEC 60751 range.
 */
Max35103Status MAX35103_PlatinumRtdToMilliCelsius(
    uint32_t resistance_milliohm, uint32_t r0_milliohm,
    int32_t *temperature_millicelsius);

/**
 * @brief Test whether a finite control/deferred operation owns the driver.
 * @param[in] drv Driver instance.
 * @return true for active reset/read/halt/self-check/temperature work.
 * @note EVENT_RUNNING alone returns false because it is a stable wait state.
 */
bool MAX35103_IsBusy(const Max35103Driver *drv);

/**
 * @brief Return the observable driver state.
 * @param[in] drv Driver instance.
 * @return Current state, or MAX35103_STATE_UNINIT when drv is NULL.
 */
Max35103State MAX35103_GetState(const Max35103Driver *drv);

/**
 * Non-destructive presence heuristic using configuration registers. SPI has
 * no acknowledgement, so a successful probe is evidence rather than identity.
 *
 * @param[in,out] drv Ready, non-busy driver instance.
 * @return true when both reads succeed and values reject common open-bus and
 *         reserved-bit patterns; false otherwise.
 *
 * @warning This is not a silicon identity check because MAX35103 exposes no
 *          dedicated device-ID register.
 */
bool MAX35103_Probe(Max35103Driver *drv);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_MAX35103_H */