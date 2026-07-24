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
  ******************************************************************************
  */

#ifndef SWFPM_MAX35103_H
#define SWFPM_MAX35103_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* SPI frame sizes. */
#define MAX35103_REGISTER_FRAME_BYTES       3U
#define MAX35103_TOF_RESULT_WORDS           7U
#define MAX35103_TOF_RESULT_FRAME_BYTES    14U
#define MAX35103_WAVE_HIT_COUNT              6U
#define MAX35103_TEMP_PORT_COUNT             4U
#define MAX35103_TEMP_RESULT_WORDS           8U
#define MAX35103_TEMP_RESULT_FRAME_BYTES    16U

/* Execution opcodes: sent as exactly one byte. */
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
#define MAX35103_TOF1_DPL_MASK           0x00F0U
#define MAX35103_TOF1_RESERVED_MASK      0x0004U
#define MAX35103_TOF1_STOP_POL_MASK      0x0008U
#define MAX35103_TOF1_CT_MASK            0x0003U
#define MAX35103_TOF2_STOP_MASK          0xE000U
#define MAX35103_TOF2_T2WV_MASK          0x1F80U
#define MAX35103_TOF2_T2WV_SHIFT              7U
#define MAX35103_TOF2_TIMEOUT_MASK       0x0007U
#define MAX35103_TOF2_RESERVED_MASK      0x0008U
#define MAX35103_TOF3_5_RESERVED_MASK    0xC0C0U
#define MAX35103_TOF_WAVE_SELECT_MASK    0x003FU
#define MAX35103_TOF6_7_RESERVED_MASK    0x0080U
#define MAX35103_CAL_CTRL_RESERVED_MASK  0xF000U
#define MAX35103_TOF_DELAY_MIN           0x0012U

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

/* Timing defaults. Zero in a profile selects the corresponding default. */
#define MAX35103_INIT_TIMEOUT_MS          100U
#define MAX35103_RESULT_TIMEOUT_MS        200U
#define MAX35103_HALT_TIMEOUT_MS          100U
#define MAX35103_SPI_TIMEOUT_MS            10U
#define MAX35103_RESET_PULSE_MS             1U
#define MAX35103_RESET_READY_MS             1U
#define MAX35103_INIT_SETTLE_MS              3U

typedef enum {
    MAX35103_OK           = 0,
    MAX35103_BUSY         = -1,
    MAX35103_TIMEOUT      = -2,
    MAX35103_INVALID_ARG  = -3,
    MAX35103_NOT_READY    = -4,
    MAX35103_SPI_ERROR    = -5,
    MAX35103_DEVICE_ERROR = -6,
    MAX35103_CONFIG_ERROR = -7,
    MAX35103_NO_RESULT    = -8,
    MAX35103_STALE        = -9,
    MAX35103_OUT_OF_RANGE = -10,
} Max35103Status;

/** Result of one platform transport operation. */
typedef enum {
    MAX35103_TRANSPORT_OK = 0,
    MAX35103_TRANSPORT_BUSY,
    MAX35103_TRANSPORT_TIMEOUT,
    MAX35103_TRANSPORT_ERROR,
} Max35103TransportStatus;

/**
 * Platform operations required by the portable core.
 *
 * transfer() owns one complete NSS-low SPI transaction. rx may be NULL for an
 * execution command. set_reset() receives true while the active-low hardware
 * reset is asserted. The driver copies this table, but context remains borrowed
 * and must outlive the driver.
 */
typedef struct {
    Max35103TransportStatus (*transfer)(
        void *context, const uint8_t *tx, uint8_t *rx,
        uint16_t length, uint32_t timeout_ms);
    Max35103TransportStatus (*set_reset)(
        void *context, bool asserted);
    uint32_t (*get_tick_ms)(void *context);
    void (*delay_ms)(void *context, uint32_t delay_ms);
    void *context;
} Max35103Transport;

typedef enum {
    MAX35103_STATE_UNINIT = 0,
    MAX35103_STATE_IDLE,
    MAX35103_STATE_ARMING,
    MAX35103_STATE_EVENT_RUNNING,
    MAX35103_STATE_DRAIN_STATUS,
    MAX35103_STATE_READ_RESULT,
    MAX35103_STATE_HALTING,
    MAX35103_STATE_SELF_CHECK,
    MAX35103_STATE_TIMEOUT,
    MAX35103_STATE_ERROR,
    MAX35103_STATE_TEMP_MEASURING,
    MAX35103_STATE_READ_TEMP_RESULT,
} Max35103State;

/** Complete active configuration image for one product/sensor variant. */
typedef struct {
    uint32_t profile_id;
    uint32_t profile_version;

    /* One of MAX35103_CMD_EVTMG1/2/3. */
    uint8_t event_mode_cmd;

    /* Every register is written, including values equal to zero. */
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

    uint32_t init_timeout_ms;
    uint32_t result_timeout_ms;
    uint32_t halt_timeout_ms;

    /*
     * Host-side temperature conversion data. Set either value to zero to
     * return raw T1..T4 timings without resistance/temperature conversion.
     * The standard System Diagram connects T1/T2 to platinum RTDs and T3/T4
     * to the same reference resistor.
     */
    uint32_t reference_resistance_milliohm;
    uint32_t rtd_nominal_resistance_milliohm;
} Max35103Profile;

/**
 * Raw register evidence plus nominal-time conversion.
 *
 * The ps fields assume an exact 4 MHz clock. Calibration/gain correction must
 * be applied by the measurement-processing layer when required.
 */
typedef struct {
    uint16_t avg_up_int;
    uint16_t avg_up_frac;
    uint16_t avg_down_int;
    uint16_t avg_down_frac;
    uint16_t tof_diff_int;
    uint16_t tof_diff_frac;
    uint16_t cycle_range_word;

    uint32_t tof_up_q16;
    uint32_t tof_down_q16;
    int32_t  tof_diff_q16;

    int64_t  tof_up_ps;
    int64_t  tof_down_ps;
    int64_t  tof_diff_ps;

    uint8_t  valid_cycle_count;
    uint8_t  tof_range;
    uint16_t status_flags;
    uint64_t timestamp_us;
    bool     valid;
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
    uint16_t wvr_up;
    uint16_t wvr_down;
    uint8_t wvr_up_t1_t2_q7;
    uint8_t wvr_up_t2_ideal_q7;
    uint8_t wvr_down_t1_t2_q7;
    uint8_t wvr_down_t2_ideal_q7;

    uint16_t hit_up_int[MAX35103_WAVE_HIT_COUNT];
    uint16_t hit_up_frac[MAX35103_WAVE_HIT_COUNT];
    uint16_t hit_down_int[MAX35103_WAVE_HIT_COUNT];
    uint16_t hit_down_frac[MAX35103_WAVE_HIT_COUNT];
    uint32_t hit_up_q16[MAX35103_WAVE_HIT_COUNT];
    uint32_t hit_down_q16[MAX35103_WAVE_HIT_COUNT];
    int64_t hit_up_ps[MAX35103_WAVE_HIT_COUNT];
    int64_t hit_down_ps[MAX35103_WAVE_HIT_COUNT];

    uint8_t configured_hit_count;
    bool valid;
} Max35103WaveEvidence;

/**
 * Temperature timing evidence and optional platinum-RTD conversion.
 *
 * Each port value is an unsigned Q16 count of 4 MHz clock periods. With the
 * standard connection, RRTD1/RREF = T1/T3 and RRTD2/RREF = T2/T4.
 */
typedef struct {
    uint16_t port_int[MAX35103_TEMP_PORT_COUNT];
    uint16_t port_frac[MAX35103_TEMP_PORT_COUNT];
    uint32_t port_q16[MAX35103_TEMP_PORT_COUNT];

    uint32_t rtd1_resistance_milliohm;
    uint32_t rtd2_resistance_milliohm;
    int32_t rtd1_temperature_millicelsius;
    int32_t rtd2_temperature_millicelsius;

    uint16_t status_flags;
    uint64_t timestamp_us;
    uint8_t selected_port_mask;
    uint8_t valid_port_mask;
    uint8_t short_circuit_mask;
    uint8_t open_circuit_mask;
    uint8_t valid_cycle_count;
    bool averaged;
    bool rtd1_valid;
    bool rtd2_valid;
    bool rtd1_temperature_valid;
    bool rtd2_temperature_valid;
    bool valid;
} Max35103TemperatureResult;

/** Borrowed view of a pending caller-driven SPI transaction. */
typedef struct {
    const uint8_t *tx;
    uint8_t *rx;
    uint16_t length;
    uint32_t token;
} Max35103SpiRequest;

/** Driver instance; allocated by the composition root. */
typedef struct {
    Max35103Transport transport;
    Max35103State state;
    uint32_t generation;
    uint64_t attempt_start_us;
    uint64_t deadline_us;

    const Max35103Profile *profile; /* Borrowed; owner must outlive driver. */
    bool device_ready;
    bool configured;
    bool event_timing_active;

    uint8_t tx_buf[MAX35103_REGISTER_FRAME_BYTES];
    uint8_t rx_buf[MAX35103_REGISTER_FRAME_BYTES];
    uint16_t spi_length;
    uint32_t spi_token;
    uint32_t next_spi_token;
    bool spi_pending;

    uint8_t result_frame[MAX35103_TOF_RESULT_FRAME_BYTES];
    uint8_t temperature_frame[MAX35103_TEMP_RESULT_FRAME_BYTES];
    uint8_t result_word_index;
    uint16_t temperature_cycle_word;
    uint16_t latched_status;
    uint64_t interrupt_timestamp_us;

    Max35103RawResult result;
    bool result_pending;
    Max35103TemperatureResult temperature_result;
    bool temperature_result_pending;

    uint32_t irq_count;
    uint32_t unexpected_irq_count;
    uint32_t spi_done_count;
    uint32_t stale_spi_completion_count;
    uint32_t timeout_count;
    uint32_t error_count;
    uint32_t result_count;
    uint32_t invalid_result_count;
    uint32_t dropped_result_count;
    uint32_t temperature_result_count;
    uint32_t invalid_temperature_result_count;
    uint32_t dropped_temperature_result_count;
} Max35103Driver;

/**
 * Initialize one driver instance with caller-owned platform operations.
 *
 * transfer(), get_tick_ms(), and delay_ms() are mandatory. set_reset() may be
 * NULL only when ResetDevice() is not used.
 */
Max35103Status MAX35103_Init(
    Max35103Driver *drv, const Max35103Transport *transport);

/**
 * Pulse hardware RST, verify POR, issue INIT, and wait for INIT completion.
 * This restores the device flash image; call Configure() afterwards to apply
 * the production profile without writing flash.
 */
Max35103Status MAX35103_ResetDevice(Max35103Driver *drv);

/**
 * Validate the register image without accessing the device.
 *
 * This rejects disabled/unsupported pulse-launch settings, reserved bits, an
 * unsupported measurement delay, and a delay longer than the TOF2 timeout.
 * It checks structural safety only; transducer-specific wave and comparator
 * settings still require board-level characterization.
 */
Max35103Status MAX35103_ValidateProfile( const Max35103Profile *profile);

/** Apply and read-verify the complete volatile configuration image. */
Max35103Status MAX35103_Configure(Max35103Driver *drv, const Max35103Profile *profile);

/** Start the configured event-timing command. */
Max35103Status MAX35103_StartEventTiming(Max35103Driver *drv);

/** Send HALT and return only after the HALT flag is observed. */
Max35103Status MAX35103_Halt(Max35103Driver *drv);

/**
 * Execute one direct TOF_DIFF measurement. On completion, the result is put
 * in the normal result mailbox and can be taken with GetResult().
 */
Max35103Status MAX35103_SelfCheck(Max35103Driver *drv);

/**
 * Execute one direct, blocking TEMPERATURE command and read T1..T4.
 * Event Timing 2 selects which ports are measured. The profile's reference
 * and nominal RTD resistance values enable resistance and IEC 60751 platinum
 * RTD conversion; raw timing remains available when those values are zero.
 */
Max35103Status MAX35103_MeasureTemperature(
    Max35103Driver *drv, Max35103TemperatureResult *result);

/**
 * Cancel only the host-side pending result read. The device event engine is
 * not halted; call Halt() when the hardware must stop.
 */
void MAX35103_Cancel(Max35103Driver *drv);

/** Record falling-edge MAX_INT evidence and schedule a status-register read. */
void MAX35103_OnInt(Max35103Driver *drv, uint64_t now_us);

/** Obtain the pending transaction for an external interrupt/DMA SPI adapter. */
bool MAX35103_GetPendingSpiRequest(Max35103Driver *drv,
                                   Max35103SpiRequest *request);

/**
 * Complete an externally executed transaction. token must match the request
 * returned by GetPendingSpiRequest(); stale completions are ignored.
 */
void MAX35103_OnSpiDone(Max35103Driver *drv, uint32_t token,
                        bool transfer_ok);

/** Execute one pending transaction through the injected blocking transport. */
Max35103Status MAX35103_ExecuteSpi(Max35103Driver *drv);

/** Check the deadline and execute at most one pending SPI transaction. */
Max35103Status MAX35103_Process(Max35103Driver *drv, uint64_t now_us);

/** Force timeout handling for the current deferred result-read operation. */
void MAX35103_OnTimeout(Max35103Driver *drv);

/* Blocking register access for initialization, diagnostics, and HIL. */
Max35103Status MAX35103_ReadReg(Max35103Driver *drv,
                                uint8_t read_opcode, uint16_t *value);
Max35103Status MAX35103_WriteReg(Max35103Driver *drv,
                                 uint8_t write_opcode, uint16_t value);
Max35103Status MAX35103_WriteVerifyReg(Max35103Driver *drv,
                                       uint8_t write_opcode, uint16_t value);

bool MAX35103_HasResult(const Max35103Driver *drv);
Max35103Status MAX35103_GetResult(Max35103Driver *drv,
                                  Max35103RawResult *result);

/** Blocking result read. Reads Interrupt Status exactly once. */
Max35103Status MAX35103_ReadResult(Max35103Driver *drv,
                                   Max35103RawResult *result);

/**
 * Read WVRUP/WVRDN and every configured HITx pair from the latest TOF result.
 *
 * This function does not read INT_STATUS and therefore does not consume
 * interrupt evidence. Call it after a successful TOF measurement, while the
 * driver is idle. It is intentionally blocking because auto-calibration runs
 * outside ISR context.
 */
Max35103Status MAX35103_ReadWaveEvidence(
    Max35103Driver *drv, Max35103WaveEvidence *evidence);

/** Return the effective number of configured hits (1..6). */
uint8_t MAX35103_ConfiguredHitCount(const Max35103Profile *profile);

bool MAX35103_HasTemperatureResult(const Max35103Driver *drv);
Max35103Status MAX35103_GetTemperatureResult(
    Max35103Driver *drv, Max35103TemperatureResult *result);

/**
 * Convert a resistance from a platinum RTD with IEC 60751 alpha=0.00385.
 * Supports the standard -200 C to +850 C range and either PT100 or PT1000
 * through the caller-provided R0 value.
 */
Max35103Status MAX35103_PlatinumRtdToMilliCelsius(
    uint32_t resistance_milliohm, uint32_t r0_milliohm,
    int32_t *temperature_millicelsius);

bool MAX35103_IsBusy(const Max35103Driver *drv);
Max35103State MAX35103_GetState(const Max35103Driver *drv);

/**
 * Non-destructive presence heuristic using configuration registers. SPI has
 * no acknowledgement, so a successful probe is evidence rather than identity.
 */
bool MAX35103_Probe(Max35103Driver *drv);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_MAX35103_H */
