/**
  ******************************************************************************
  * @file    zssc3241.h
 * @brief   Portable I2C driver for the Renesas ZSSC3241 sensor conditioner
  ******************************************************************************
  *
 * The core is platform-independent. Bus, reset, tick, and delay operations are
 * supplied through Zssc3241Transport. Platform-specific code, such as STM32
 * HAL, belongs in a separate adapter.
  *
  * The driver supports Command, Sleep, and Cyclic modes; raw and corrected
  * measurements; oversampling; diagnostics; protected NVM access; temporary
  * shadow-register overwrite; blocking calls; and deferred EOC processing.
  ******************************************************************************
  */

#ifndef SWFPM_ZSSC3241_H
#define SWFPM_ZSSC3241_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* I2C response sizes, including the leading status byte. */
#define ZSSC3241_STATUS_FRAME_BYTES          1U
#define ZSSC3241_WORD_FRAME_BYTES            3U
#define ZSSC3241_RAW_FRAME_BYTES             4U
#define ZSSC3241_MEASUREMENT_FRAME_BYTES     7U
#define ZSSC3241_MAX_RESPONSE_BYTES          7U

/* Command list supported by the I2C driver. */
#define ZSSC3241_CMD_NVM_READ_MIN            0x00U
#define ZSSC3241_CMD_NVM_READ_MAX            0x3FU
#define ZSSC3241_CMD_NVM_WRITE_BASE          0x40U
#define ZSSC3241_CMD_NVM_WRITE_MAX           0x75U
#define ZSSC3241_CMD_CALCULATE_NVM_CHECKSUM  0x90U
#define ZSSC3241_CMD_RAW_SENSOR              0xA2U
#define ZSSC3241_CMD_RAW_TEMPERATURE         0xA4U
#define ZSSC3241_CMD_START_SLEEP             0xA8U
#define ZSSC3241_CMD_START_COMMAND_MODE      0xA9U
#define ZSSC3241_CMD_MEASURE                 0xAAU
#define ZSSC3241_CMD_START_CYCLIC            0xABU
#define ZSSC3241_CMD_OVERSAMPLE_2            0xACU
#define ZSSC3241_CMD_OVERSAMPLE_4            0xADU
#define ZSSC3241_CMD_OVERSAMPLE_8            0xAEU
#define ZSSC3241_CMD_OVERSAMPLE_16           0xAFU
#define ZSSC3241_CMD_CHECK_DIAG              0xB0U
#define ZSSC3241_CMD_RESET_DIAG              0xB1U
#define ZSSC3241_CMD_UPDATE_DIAG             0xB2U
#define ZSSC3241_CMD_DAC_DIAG                0xB3U
#define ZSSC3241_CMD_SELF_DIAG_MEASURE       0xB4U
#define ZSSC3241_CMD_POST_CAL_OFFSET         0xD1U
#define ZSSC3241_CMD_OVERWRITE_SM_CONFIG1    0xD6U
#define ZSSC3241_CMD_OVERWRITE_SM_CONFIG2    0xD7U
#define ZSSC3241_CMD_OVERWRITE_T_CONFIG1     0xD8U
#define ZSSC3241_CMD_OVERWRITE_T_CONFIG2     0xD9U
#define ZSSC3241_CMD_OVERWRITE_SSF1          0xDAU
#define ZSSC3241_CMD_OVERWRITE_SSF2          0xDBU

/* General status byte. */
#define ZSSC3241_STATUS_RESERVED_MASK         0x80U
#define ZSSC3241_STATUS_POWERED_MASK          0x40U
#define ZSSC3241_STATUS_BUSY_MASK             0x20U
#define ZSSC3241_STATUS_MODE_MASK             0x18U
#define ZSSC3241_STATUS_MODE_COMMAND          0x00U
#define ZSSC3241_STATUS_MODE_CYCLIC           0x08U
#define ZSSC3241_STATUS_MODE_SLEEP            0x10U
#define ZSSC3241_STATUS_MEMORY_ERROR_MASK     0x04U
#define ZSSC3241_STATUS_CONNECTION_FAULT_MASK 0x02U
#define ZSSC3241_STATUS_MATH_SATURATION_MASK  0x01U
#define ZSSC3241_STATUS_FAULT_MASK            0x07U

/* Detailed diagnosticreg bits returned by CHECK_DIAG (0xB0). */
#define ZSSC3241_DIAG_FAULT_IMPROVED          0x0001U
#define ZSSC3241_DIAG_INP_OPEN                0x0002U
#define ZSSC3241_DIAG_INN_OPEN                0x0004U
#define ZSSC3241_DIAG_INP_OUT_OF_RANGE        0x0008U
#define ZSSC3241_DIAG_INN_OUT_OF_RANGE        0x0010U
#define ZSSC3241_DIAG_SENSOR_SHORT            0x0020U
#define ZSSC3241_DIAG_TEXT_OPEN               0x0040U
#define ZSSC3241_DIAG_TEXT_OUT_OF_RANGE       0x0080U
#define ZSSC3241_DIAG_TEXT_SHORT_TO_INN       0x0100U
#define ZSSC3241_DIAG_MATH_SATURATION         0x0200U
#define ZSSC3241_DIAG_MEMORY_ERROR            0x0400U
#define ZSSC3241_DIAG_TEXT_SHORT_TO_INP       0x0800U
#define ZSSC3241_DIAG_CHIPPING_FAILURE        0x1000U
#define ZSSC3241_DIAG_FAULT_MASK              0x1FFEU

/* NVM layout limits and selected addresses used for protection. */
#define ZSSC3241_NVM_READ_MIN                 0x00U
#define ZSSC3241_NVM_READ_MAX                 0x3FU
#define ZSSC3241_NVM_CUSTOM_WRITE_MAX         0x35U
#define ZSSC3241_NVM_WORD_COUNT               64U
#define ZSSC3241_NVM_INTERFACE_CONFIG         0x02U
#define ZSSC3241_NVM_SSF1                     0x03U
#define ZSSC3241_NVM_SSF2                     0x04U
#define ZSSC3241_NVM_LOCK_MASK                0x4000U
#define ZSSC3241_NVM_SLAVE_ADDRESS_MASK       0x007FU

/* Two independent opt-ins are required before an NVM write is accepted. */
#define ZSSC3241_NVM_UNLOCK_KEY                UINT32_C(0x5A3241A5)

/* Default timings. A zero field in Zssc3241Config selects the default. */
#define ZSSC3241_DEFAULT_BUS_TIMEOUT_MS        10U
#define ZSSC3241_DEFAULT_MEASUREMENT_TIMEOUT_MS 25U
#define ZSSC3241_DEFAULT_DIAGNOSTIC_TIMEOUT_MS 50U
#define ZSSC3241_DEFAULT_NVM_TIMEOUT_MS        25U
#define ZSSC3241_DEFAULT_POLL_INTERVAL_MS       1U
#define ZSSC3241_DEFAULT_RESET_PULSE_MS         1U
#define ZSSC3241_DEFAULT_RESET_READY_MS         5U

typedef enum {
    ZSSC3241_OK                    = 0,
    ZSSC3241_BUSY                  = -1,
    ZSSC3241_TIMEOUT               = -2,
    ZSSC3241_INVALID_ARG           = -3,
    ZSSC3241_NOT_INITIALIZED       = -4,
    ZSSC3241_I2C_ERROR             = -5,
    ZSSC3241_NACK                  = -6,
    ZSSC3241_NOT_READY             = -7,
    ZSSC3241_INVALID_RESPONSE      = -8,
    ZSSC3241_WRONG_MODE            = -9,
    ZSSC3241_MEMORY_ERROR          = -10,
    ZSSC3241_CONNECTION_FAULT      = -11,
    ZSSC3241_MATH_SATURATION       = -12,
    ZSSC3241_NVM_WRITE_DISABLED    = -13,
    ZSSC3241_NVM_LOCKED            = -14,
    ZSSC3241_NVM_LOCK_PROTECTED    = -15,
    ZSSC3241_NVM_VERIFY_FAILED     = -16,
    ZSSC3241_NO_RESULT             = -17,
    ZSSC3241_STALE_RESULT          = -18,
    ZSSC3241_UNSUPPORTED           = -19,
    ZSSC3241_OUT_OF_RANGE          = -20,
} Zssc3241Status;

/** Result of one platform transport operation. */
typedef enum {
    ZSSC3241_TRANSPORT_OK = 0,
    ZSSC3241_TRANSPORT_BUSY,
    ZSSC3241_TRANSPORT_TIMEOUT,
    ZSSC3241_TRANSPORT_NACK,
    ZSSC3241_TRANSPORT_ERROR,
} Zssc3241TransportStatus;

/**
 * Platform operations required by the portable core.
 *
 * The driver copies this table, but context remains caller-owned and must
 * outlive the Zssc3241 instance. write(), read(), get_tick_ms(), and delay_ms()
 * are mandatory. set_reset() is optional when RESQ is not controlled by the
 * host.
 */
typedef struct {
    Zssc3241TransportStatus (*write)(
        void *context, uint8_t address_7bit,
        const uint8_t *data, uint16_t length,
        uint32_t timeout_ms);
    Zssc3241TransportStatus (*read)(
        void *context, uint8_t address_7bit,
        uint8_t *data, uint16_t length,
        uint32_t timeout_ms);
    Zssc3241TransportStatus (*set_reset)(
        void *context, bool asserted);
    uint32_t (*get_tick_ms)(void *context);
    void (*delay_ms)(void *context, uint32_t delay_ms);
    void *context;
} Zssc3241Transport;

typedef enum {
    ZSSC3241_MODE_UNKNOWN = 0,
    ZSSC3241_MODE_COMMAND,
    ZSSC3241_MODE_CYCLIC,
    ZSSC3241_MODE_SLEEP,
} Zssc3241Mode;

typedef enum {
    ZSSC3241_STATE_UNINITIALIZED = 0,
    ZSSC3241_STATE_IDLE,
    ZSSC3241_STATE_WAIT_READY,
    ZSSC3241_STATE_CYCLIC,
    ZSSC3241_STATE_TIMEOUT,
    ZSSC3241_STATE_ERROR,
} Zssc3241State;

typedef enum {
    ZSSC3241_MEASUREMENT_RAW_SENSOR = 0,
    ZSSC3241_MEASUREMENT_RAW_TEMPERATURE,
    ZSSC3241_MEASUREMENT_CORRECTED,
    ZSSC3241_MEASUREMENT_OVERSAMPLE_2,
    ZSSC3241_MEASUREMENT_OVERSAMPLE_4,
    ZSSC3241_MEASUREMENT_OVERSAMPLE_8,
    ZSSC3241_MEASUREMENT_OVERSAMPLE_16,
    ZSSC3241_MEASUREMENT_CYCLIC,
    ZSSC3241_MEASUREMENT_SELF_DIAGNOSTIC,
} Zssc3241MeasurementType;

typedef enum {
    ZSSC3241_SHADOW_SM_CONFIG1 = ZSSC3241_CMD_OVERWRITE_SM_CONFIG1,
    ZSSC3241_SHADOW_SM_CONFIG2 = ZSSC3241_CMD_OVERWRITE_SM_CONFIG2,
    ZSSC3241_SHADOW_T_CONFIG1  = ZSSC3241_CMD_OVERWRITE_T_CONFIG1,
    ZSSC3241_SHADOW_T_CONFIG2  = ZSSC3241_CMD_OVERWRITE_T_CONFIG2,
    ZSSC3241_SHADOW_SSF1       = ZSSC3241_CMD_OVERWRITE_SSF1,
    ZSSC3241_SHADOW_SSF2       = ZSSC3241_CMD_OVERWRITE_SSF2,
} Zssc3241ShadowRegister;

typedef struct {
    uint32_t bus_timeout_ms;
    uint32_t measurement_timeout_ms;
    uint32_t diagnostic_timeout_ms;
    uint32_t nvm_timeout_ms;
    uint32_t poll_interval_ms;
    uint32_t reset_pulse_ms;
    uint32_t reset_ready_ms;

    bool use_eoc_interrupt;
    bool reject_faulty_measurements;
    bool allow_nvm_write;
    bool verify_nvm_after_write;
} Zssc3241Config;

typedef struct {
    uint8_t raw;
    Zssc3241Mode mode;
    bool powered;
    bool busy;
    bool memory_error;
    bool connection_fault;
    bool math_saturation;
} Zssc3241DeviceStatus;

typedef struct {
    Zssc3241MeasurementType type;
    Zssc3241Status operation_status;
    Zssc3241DeviceStatus device_status;

    /* Original 24-bit payloads are retained in the least-significant bits. */
    uint32_t sensor_raw24;
    uint32_t temperature_raw24;

    /* Valid for raw measurements; corrected results are unsigned. */
    int32_t sensor_signed24;
    int32_t temperature_signed24;

    uint32_t generation;
    uint32_t timestamp_ms;
    bool sensor_valid;
    bool temperature_valid;
    bool corrected;
    bool valid;
} Zssc3241Measurement;

typedef struct {
    Zssc3241DeviceStatus device_status;
    uint16_t raw;
    bool valid;
} Zssc3241Diagnostics;

typedef struct {
    Zssc3241Transport transport;
    Zssc3241Config config;
    uint8_t address_7bit;

    Zssc3241State state;
    Zssc3241Mode mode;
    Zssc3241MeasurementType pending_measurement;
    uint8_t pending_command;
    uint8_t pending_response_length;
    uint32_t operation_start_ms;
    uint32_t operation_timeout_ms;
    uint32_t next_poll_ms;

    volatile bool eoc_pending;
    bool initialized;
    bool nvm_write_unlocked;

    Zssc3241Measurement result;
    bool result_pending;
    uint32_t generation;
    Zssc3241Status last_error;

    uint32_t command_count;
    uint32_t result_count;
    uint32_t dropped_result_count;
    uint32_t timeout_count;
    uint32_t nack_count;
    uint32_t i2c_error_count;
    uint32_t device_fault_count;
    uint32_t invalid_response_count;
    uint32_t eoc_count;
    uint32_t unexpected_eoc_count;
    uint32_t nvm_write_count;
    uint32_t nvm_verify_failure_count;
} Zssc3241;

Zssc3241Config ZSSC3241_DefaultConfig(void);

/**
 * Initialize one driver instance with caller-owned platform operations.
 *
 * The transport table is copied. Its context and all referenced platform
 * resources remain caller-owned and must outlive the driver.
 */
Zssc3241Status ZSSC3241_Init(Zssc3241 *device,
                              const Zssc3241Transport *transport,
                              uint8_t address_7bit,
                              const Zssc3241Config *config);
Zssc3241Status ZSSC3241_Probe(Zssc3241 *device);
Zssc3241Status ZSSC3241_Reset(Zssc3241 *device);
Zssc3241Status ZSSC3241_ReadStatus(Zssc3241 *device,
                                    Zssc3241DeviceStatus *status);

Zssc3241Status ZSSC3241_EnterCommandMode(Zssc3241 *device);
Zssc3241Status ZSSC3241_EnterSleepMode(Zssc3241 *device);
Zssc3241Status ZSSC3241_StartCyclicMode(Zssc3241 *device);
Zssc3241Status ZSSC3241_StopCyclicMode(Zssc3241 *device);

Zssc3241Status ZSSC3241_StartMeasurement(
    Zssc3241 *device, Zssc3241MeasurementType type);
Zssc3241Status ZSSC3241_Process(Zssc3241 *device);
void ZSSC3241_OnEocInterrupt(Zssc3241 *device);
void ZSSC3241_Cancel(Zssc3241 *device);
bool ZSSC3241_IsBusy(const Zssc3241 *device);
Zssc3241State ZSSC3241_GetState(const Zssc3241 *device);
bool ZSSC3241_HasResult(const Zssc3241 *device);
Zssc3241Status ZSSC3241_GetLatestResult(
    Zssc3241 *device, Zssc3241Measurement *result);

Zssc3241Status ZSSC3241_Measure(
    Zssc3241 *device, Zssc3241Measurement *result);
Zssc3241Status ZSSC3241_MeasureRawSensor(
    Zssc3241 *device, Zssc3241Measurement *result);
Zssc3241Status ZSSC3241_MeasureRawTemperature(
    Zssc3241 *device, Zssc3241Measurement *result);
Zssc3241Status ZSSC3241_MeasureOversampled(
    Zssc3241 *device, uint8_t sample_count, Zssc3241Measurement *result);
Zssc3241Status ZSSC3241_ReadCyclicResult(
    Zssc3241 *device, Zssc3241Measurement *result);

Zssc3241Status ZSSC3241_ReadDiagnostics(
    Zssc3241 *device, Zssc3241Diagnostics *diagnostics);
Zssc3241Status ZSSC3241_ResetDiagnostics(Zssc3241 *device);
Zssc3241Status ZSSC3241_UpdateDiagnostics(Zssc3241 *device);
Zssc3241Status ZSSC3241_SetDacDiagnostic(
    Zssc3241 *device, uint16_t dac_value);
Zssc3241Status ZSSC3241_RunSelfDiagnostic(
    Zssc3241 *device, uint8_t pseudo_offset,
    Zssc3241Measurement *result);

Zssc3241Status ZSSC3241_ReadNvm(
    Zssc3241 *device, uint8_t address, uint16_t *value);
Zssc3241Status ZSSC3241_DumpNvm(
    Zssc3241 *device, uint16_t *buffer, size_t word_count);
Zssc3241Status ZSSC3241_UnlockNvmWrites(
    Zssc3241 *device, uint32_t unlock_key);
void ZSSC3241_LockNvmWrites(Zssc3241 *device);
Zssc3241Status ZSSC3241_WriteNvm(
    Zssc3241 *device, uint8_t address, uint16_t value);
Zssc3241Status ZSSC3241_WriteNvmBlock(
    Zssc3241 *device, uint8_t start_address,
    const uint16_t *values, size_t word_count,
    bool update_checksum);
Zssc3241Status ZSSC3241_UpdateNvmChecksum(Zssc3241 *device);

Zssc3241Status ZSSC3241_OverwriteShadow(
    Zssc3241 *device, Zssc3241ShadowRegister reg, uint16_t value);
Zssc3241Status ZSSC3241_SetPostCalibrationOffset(
    Zssc3241 *device, uint16_t expected_output);

int32_t ZSSC3241_DecodeSigned24(uint32_t raw24);
uint32_t ZSSC3241_DecodeUnsigned24(const uint8_t data[3]);
float ZSSC3241_CorrectedToNormalized(uint32_t corrected_raw24);
float ZSSC3241_RawToNormalized(int32_t signed_raw24);
Zssc3241Status ZSSC3241_MapCorrected(
    uint32_t corrected_raw24,
    uint32_t code_min, uint32_t code_max,
    int32_t physical_min, int32_t physical_max,
    int32_t *physical_value);

const char *ZSSC3241_StatusString(Zssc3241Status status);

#ifdef __cplusplus
}
#endif

#endif /* SWFPM_ZSSC3241_H */
