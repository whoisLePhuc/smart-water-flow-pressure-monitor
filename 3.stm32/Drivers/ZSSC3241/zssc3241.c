/**
  ******************************************************************************
  * @file    zssc3241.c
  * @brief   Embedded I2C driver for the Renesas ZSSC3241 sensor conditioner
  ******************************************************************************
  */

#include "zssc3241.h"

#include <limits.h>
#include <string.h>

static uint32_t zssc_value_or_default(uint32_t value, uint32_t fallback)
{
    return value == 0U ? fallback : value;
}

static bool zssc_device_valid(const Zssc3241 *device)
{
    return device != NULL &&
           device->initialized &&
           device->transport.write != NULL &&
           device->transport.read != NULL &&
           device->transport.get_tick_ms != NULL &&
           device->transport.delay_ms != NULL;
}

static uint32_t zssc_now(const Zssc3241 *device)
{
    return device->transport.get_tick_ms(device->transport.context);
}

static bool zssc_elapsed(uint32_t now, uint32_t start, uint32_t timeout)
{
    return (uint32_t)(now - start) >= timeout;
}

static bool zssc_time_reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

static void zssc_delay(Zssc3241 *device, uint32_t delay_ms)
{
    if (delay_ms != 0U) {
        device->transport.delay_ms(
            device->transport.context, delay_ms);
    }
}

static Zssc3241Status zssc_transport_status(
    Zssc3241 *device, Zssc3241TransportStatus transport_status)
{
    if (transport_status == ZSSC3241_TRANSPORT_OK) {
        return ZSSC3241_OK;
    }
    if (transport_status == ZSSC3241_TRANSPORT_BUSY) {
        return ZSSC3241_BUSY;
    }
    if (transport_status == ZSSC3241_TRANSPORT_TIMEOUT) {
        device->timeout_count++;
        return ZSSC3241_TIMEOUT;
    }
    if (transport_status == ZSSC3241_TRANSPORT_NACK) {
        device->nack_count++;
        return ZSSC3241_NACK;
    }
    device->i2c_error_count++;
    return ZSSC3241_I2C_ERROR;
}

static Zssc3241Status zssc_write(Zssc3241 *device,
                                 const uint8_t *data, uint16_t length)
{
    Zssc3241TransportStatus transport_status = device->transport.write(
        device->transport.context,
        device->address_7bit,
        data,
        length,
        device->config.bus_timeout_ms);
    return zssc_transport_status(device, transport_status);
}

static Zssc3241Status zssc_read(Zssc3241 *device,
                                uint8_t *data, uint16_t length)
{
    Zssc3241TransportStatus transport_status = device->transport.read(
        device->transport.context,
        device->address_7bit,
        data,
        length,
        device->config.bus_timeout_ms);
    return zssc_transport_status(device, transport_status);
}

static Zssc3241Status zssc_send_command(Zssc3241 *device,
                                        uint8_t command,
                                        bool has_data,
                                        uint16_t command_data)
{
    uint8_t frame[3];
    uint16_t length = 1U;

    frame[0] = command;
    if (has_data) {
        frame[1] = (uint8_t)(command_data >> 8);
        frame[2] = (uint8_t)command_data;
        length = 3U;
    }

    Zssc3241Status status = zssc_write(device, frame, length);
    if (status == ZSSC3241_OK) {
        device->command_count++;
    }
    return status;
}

static Zssc3241Mode zssc_decode_mode(uint8_t raw_status)
{
    switch (raw_status & ZSSC3241_STATUS_MODE_MASK) {
    case ZSSC3241_STATUS_MODE_COMMAND:
        return ZSSC3241_MODE_COMMAND;
    case ZSSC3241_STATUS_MODE_CYCLIC:
        return ZSSC3241_MODE_CYCLIC;
    case ZSSC3241_STATUS_MODE_SLEEP:
        return ZSSC3241_MODE_SLEEP;
    default:
        return ZSSC3241_MODE_UNKNOWN;
    }
}

static void zssc_decode_status_byte(uint8_t raw,
                                    Zssc3241DeviceStatus *status)
{
    memset(status, 0, sizeof(*status));
    status->raw = raw;
    status->mode = zssc_decode_mode(raw);
    status->powered = (raw & ZSSC3241_STATUS_POWERED_MASK) != 0U;
    status->busy = (raw & ZSSC3241_STATUS_BUSY_MASK) != 0U;
    status->memory_error =
        (raw & ZSSC3241_STATUS_MEMORY_ERROR_MASK) != 0U;
    status->connection_fault =
        (raw & ZSSC3241_STATUS_CONNECTION_FAULT_MASK) != 0U;
    status->math_saturation =
        (raw & ZSSC3241_STATUS_MATH_SATURATION_MASK) != 0U;
}

static Zssc3241Status zssc_validate_status_byte(
    uint8_t raw, Zssc3241DeviceStatus *decoded)
{
    if ((raw & ZSSC3241_STATUS_RESERVED_MASK) != 0U) {
        return ZSSC3241_INVALID_RESPONSE;
    }

    zssc_decode_status_byte(raw, decoded);
    if (!decoded->powered || decoded->mode == ZSSC3241_MODE_UNKNOWN) {
        return ZSSC3241_NOT_READY;
    }
    return ZSSC3241_OK;
}

static Zssc3241Status zssc_fault_status(
    const Zssc3241DeviceStatus *status)
{
    if (status->memory_error) {
        return ZSSC3241_MEMORY_ERROR;
    }
    if (status->connection_fault) {
        return ZSSC3241_CONNECTION_FAULT;
    }
    if (status->math_saturation) {
        return ZSSC3241_MATH_SATURATION;
    }
    return ZSSC3241_OK;
}

static Zssc3241Status zssc_read_status_internal(
    Zssc3241 *device, Zssc3241DeviceStatus *decoded)
{
    uint8_t raw = 0U;
    Zssc3241Status status = zssc_read(device, &raw, 1U);
    if (status != ZSSC3241_OK) {
        return status;
    }

    status = zssc_validate_status_byte(raw, decoded);
    if (status == ZSSC3241_INVALID_RESPONSE) {
        device->invalid_response_count++;
    }
    if (status == ZSSC3241_OK || status == ZSSC3241_NOT_READY) {
        device->mode = decoded->mode;
    }
    return status;
}

static Zssc3241Status zssc_execute_command(
    Zssc3241 *device,
    uint8_t command,
    bool has_data,
    uint16_t command_data,
    uint8_t *response,
    uint8_t response_length,
    uint32_t timeout_ms,
    bool evaluate_faults)
{
    if (!zssc_device_valid(device) || response == NULL ||
        response_length == 0U ||
        response_length > ZSSC3241_MAX_RESPONSE_BYTES) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }
    if (device->state == ZSSC3241_STATE_WAIT_READY) {
        return ZSSC3241_BUSY;
    }

    Zssc3241Status status = zssc_send_command(
        device, command, has_data, command_data);
    if (status != ZSSC3241_OK) {
        device->last_error = status;
        return status;
    }

    const uint32_t start = zssc_now(device);
    Zssc3241DeviceStatus decoded;

    for (;;) {
        const uint32_t now = zssc_now(device);
        if (zssc_elapsed(now, start, timeout_ms)) {
            device->timeout_count++;
            device->last_error = ZSSC3241_TIMEOUT;
            return ZSSC3241_TIMEOUT;
        }

        status = zssc_read_status_internal(device, &decoded);
        if (status != ZSSC3241_OK) {
            device->last_error = status;
            return status;
        }
        if (decoded.busy) {
            zssc_delay(device, device->config.poll_interval_ms);
            continue;
        }

        if (response_length == ZSSC3241_STATUS_FRAME_BYTES) {
            response[0] = decoded.raw;
        } else {
            status = zssc_read(device, response, response_length);
            if (status != ZSSC3241_OK) {
                device->last_error = status;
                return status;
            }
            status = zssc_validate_status_byte(response[0], &decoded);
            if (status != ZSSC3241_OK || decoded.busy) {
                device->invalid_response_count++;
                device->last_error = status == ZSSC3241_OK
                    ? ZSSC3241_INVALID_RESPONSE : status;
                return device->last_error;
            }
        }

        device->mode = decoded.mode;
        status = evaluate_faults ? zssc_fault_status(&decoded)
                                 : ZSSC3241_OK;
        if (status != ZSSC3241_OK) {
            device->device_fault_count++;
        }
        device->last_error = status;
        return status;
    }
}

static bool zssc_measurement_parameters(
    Zssc3241MeasurementType type,
    uint8_t *command,
    uint8_t *response_length,
    uint8_t *sample_count)
{
    *sample_count = 1U;
    switch (type) {
    case ZSSC3241_MEASUREMENT_RAW_SENSOR:
        *command = ZSSC3241_CMD_RAW_SENSOR;
        *response_length = ZSSC3241_RAW_FRAME_BYTES;
        return true;
    case ZSSC3241_MEASUREMENT_RAW_TEMPERATURE:
        *command = ZSSC3241_CMD_RAW_TEMPERATURE;
        *response_length = ZSSC3241_RAW_FRAME_BYTES;
        return true;
    case ZSSC3241_MEASUREMENT_CORRECTED:
        *command = ZSSC3241_CMD_MEASURE;
        *response_length = ZSSC3241_MEASUREMENT_FRAME_BYTES;
        return true;
    case ZSSC3241_MEASUREMENT_OVERSAMPLE_2:
        *command = ZSSC3241_CMD_OVERSAMPLE_2;
        *response_length = ZSSC3241_MEASUREMENT_FRAME_BYTES;
        *sample_count = 2U;
        return true;
    case ZSSC3241_MEASUREMENT_OVERSAMPLE_4:
        *command = ZSSC3241_CMD_OVERSAMPLE_4;
        *response_length = ZSSC3241_MEASUREMENT_FRAME_BYTES;
        *sample_count = 4U;
        return true;
    case ZSSC3241_MEASUREMENT_OVERSAMPLE_8:
        *command = ZSSC3241_CMD_OVERSAMPLE_8;
        *response_length = ZSSC3241_MEASUREMENT_FRAME_BYTES;
        *sample_count = 8U;
        return true;
    case ZSSC3241_MEASUREMENT_OVERSAMPLE_16:
        *command = ZSSC3241_CMD_OVERSAMPLE_16;
        *response_length = ZSSC3241_MEASUREMENT_FRAME_BYTES;
        *sample_count = 16U;
        return true;
    default:
        return false;
    }
}

static Zssc3241Status zssc_decode_measurement(
    Zssc3241 *device,
    Zssc3241MeasurementType type,
    const uint8_t *frame,
    uint8_t frame_length,
    Zssc3241Measurement *result)
{
    memset(result, 0, sizeof(*result));
    result->type = type;
    result->generation = ++device->generation;
    result->timestamp_ms = zssc_now(device);

    Zssc3241Status status = zssc_validate_status_byte(
        frame[0], &result->device_status);
    if (status != ZSSC3241_OK || result->device_status.busy) {
        result->operation_status = status == ZSSC3241_OK
            ? ZSSC3241_INVALID_RESPONSE : status;
        return result->operation_status;
    }

    switch (type) {
    case ZSSC3241_MEASUREMENT_RAW_SENSOR:
    case ZSSC3241_MEASUREMENT_SELF_DIAGNOSTIC:
        if (frame_length != ZSSC3241_RAW_FRAME_BYTES) {
            result->operation_status = ZSSC3241_INVALID_RESPONSE;
            return result->operation_status;
        }
        result->sensor_raw24 = ZSSC3241_DecodeUnsigned24(&frame[1]);
        result->sensor_signed24 =
            ZSSC3241_DecodeSigned24(result->sensor_raw24);
        result->sensor_valid = true;
        break;
    case ZSSC3241_MEASUREMENT_RAW_TEMPERATURE:
        if (frame_length != ZSSC3241_RAW_FRAME_BYTES) {
            result->operation_status = ZSSC3241_INVALID_RESPONSE;
            return result->operation_status;
        }
        result->temperature_raw24 =
            ZSSC3241_DecodeUnsigned24(&frame[1]);
        result->temperature_signed24 =
            ZSSC3241_DecodeSigned24(result->temperature_raw24);
        result->temperature_valid = true;
        break;
    case ZSSC3241_MEASUREMENT_CORRECTED:
    case ZSSC3241_MEASUREMENT_OVERSAMPLE_2:
    case ZSSC3241_MEASUREMENT_OVERSAMPLE_4:
    case ZSSC3241_MEASUREMENT_OVERSAMPLE_8:
    case ZSSC3241_MEASUREMENT_OVERSAMPLE_16:
    case ZSSC3241_MEASUREMENT_CYCLIC:
        if (frame_length != ZSSC3241_MEASUREMENT_FRAME_BYTES) {
            result->operation_status = ZSSC3241_INVALID_RESPONSE;
            return result->operation_status;
        }
        result->sensor_raw24 = ZSSC3241_DecodeUnsigned24(&frame[1]);
        result->temperature_raw24 = ZSSC3241_DecodeUnsigned24(&frame[4]);
        result->sensor_valid = true;
        result->temperature_valid = true;
        result->corrected = true;
        break;
    default:
        result->operation_status = ZSSC3241_UNSUPPORTED;
        return result->operation_status;
    }

    status = zssc_fault_status(&result->device_status);
    result->operation_status = status;
    result->valid = status == ZSSC3241_OK &&
                    (result->sensor_valid || result->temperature_valid);
    if (status != ZSSC3241_OK) {
        device->device_fault_count++;
    }
    return status;
}

static void zssc_publish_result(Zssc3241 *device,
                                const Zssc3241Measurement *result)
{
    if (device->result_pending) {
        device->dropped_result_count++;
    }
    device->result = *result;
    device->result_pending = true;
    device->result_count++;
}

static Zssc3241Status zssc_read_measurement_frame(
    Zssc3241 *device,
    Zssc3241MeasurementType type,
    uint8_t response_length,
    Zssc3241Measurement *result)
{
    uint8_t frame[ZSSC3241_MAX_RESPONSE_BYTES] = {0U};
    Zssc3241Status status = zssc_read(device, frame, response_length);
    if (status != ZSSC3241_OK) {
        return status;
    }

    status = zssc_decode_measurement(
        device, type, frame, response_length, result);
    if (status == ZSSC3241_INVALID_RESPONSE ||
        status == ZSSC3241_NOT_READY) {
        device->invalid_response_count++;
    }
    return status;
}

static Zssc3241Status zssc_require_non_cyclic(Zssc3241 *device)
{
    if (!zssc_device_valid(device)) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }
    if (device->state == ZSSC3241_STATE_WAIT_READY) {
        return ZSSC3241_BUSY;
    }
    if (device->mode == ZSSC3241_MODE_UNKNOWN) {
        Zssc3241DeviceStatus status;
        Zssc3241Status result = ZSSC3241_ReadStatus(device, &status);
        if (result != ZSSC3241_OK) {
            return result;
        }
    }
    return device->mode == ZSSC3241_MODE_CYCLIC
               ? ZSSC3241_WRONG_MODE : ZSSC3241_OK;
}

static Zssc3241Status zssc_require_command_mode(Zssc3241 *device)
{
    Zssc3241Status status = zssc_require_non_cyclic(device);
    if (status != ZSSC3241_OK) {
        return status;
    }
    return device->mode == ZSSC3241_MODE_COMMAND
               ? ZSSC3241_OK : ZSSC3241_WRONG_MODE;
}

static Zssc3241Status zssc_measure_blocking(
    Zssc3241 *device,
    Zssc3241MeasurementType type,
    Zssc3241Measurement *result)
{
    if (result == NULL) {
        return ZSSC3241_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));

    Zssc3241Status status = ZSSC3241_StartMeasurement(device, type);
    if (status != ZSSC3241_OK) {
        return status;
    }

    while (ZSSC3241_IsBusy(device)) {
        status = ZSSC3241_Process(device);
        if (status == ZSSC3241_BUSY) {
            zssc_delay(device, device->config.poll_interval_ms);
            continue;
        }
        break;
    }

    if (ZSSC3241_HasResult(device)) {
        Zssc3241Status result_status =
            ZSSC3241_GetLatestResult(device, result);
        if (status == ZSSC3241_OK || status == ZSSC3241_BUSY) {
            status = result_status;
        }
    }
    return status;
}

Zssc3241Config ZSSC3241_DefaultConfig(void)
{
    Zssc3241Config config;
    memset(&config, 0, sizeof(config));
    config.bus_timeout_ms = ZSSC3241_DEFAULT_BUS_TIMEOUT_MS;
    config.measurement_timeout_ms =
        ZSSC3241_DEFAULT_MEASUREMENT_TIMEOUT_MS;
    config.diagnostic_timeout_ms =
        ZSSC3241_DEFAULT_DIAGNOSTIC_TIMEOUT_MS;
    config.nvm_timeout_ms = ZSSC3241_DEFAULT_NVM_TIMEOUT_MS;
    config.poll_interval_ms = ZSSC3241_DEFAULT_POLL_INTERVAL_MS;
    config.reset_pulse_ms = ZSSC3241_DEFAULT_RESET_PULSE_MS;
    config.reset_ready_ms = ZSSC3241_DEFAULT_RESET_READY_MS;
    config.use_eoc_interrupt = false;
    config.reject_faulty_measurements = true;
    config.allow_nvm_write = false;
    config.verify_nvm_after_write = true;
    return config;
}

Zssc3241Status ZSSC3241_Init(Zssc3241 *device,
                              const Zssc3241Transport *transport,
                              uint8_t address_7bit,
                              const Zssc3241Config *config)
{
    if (device == NULL || transport == NULL ||
        transport->write == NULL || transport->read == NULL ||
        transport->get_tick_ms == NULL || transport->delay_ms == NULL ||
        address_7bit > 0x7FU) {
        return ZSSC3241_INVALID_ARG;
    }

    memset(device, 0, sizeof(*device));
    device->transport = *transport;
    device->config = config != NULL ? *config : ZSSC3241_DefaultConfig();

    device->config.bus_timeout_ms = zssc_value_or_default(
        device->config.bus_timeout_ms, ZSSC3241_DEFAULT_BUS_TIMEOUT_MS);
    device->config.measurement_timeout_ms = zssc_value_or_default(
        device->config.measurement_timeout_ms,
        ZSSC3241_DEFAULT_MEASUREMENT_TIMEOUT_MS);
    device->config.diagnostic_timeout_ms = zssc_value_or_default(
        device->config.diagnostic_timeout_ms,
        ZSSC3241_DEFAULT_DIAGNOSTIC_TIMEOUT_MS);
    device->config.nvm_timeout_ms = zssc_value_or_default(
        device->config.nvm_timeout_ms, ZSSC3241_DEFAULT_NVM_TIMEOUT_MS);
    device->config.poll_interval_ms = zssc_value_or_default(
        device->config.poll_interval_ms,
        ZSSC3241_DEFAULT_POLL_INTERVAL_MS);
    device->config.reset_pulse_ms = zssc_value_or_default(
        device->config.reset_pulse_ms, ZSSC3241_DEFAULT_RESET_PULSE_MS);
    device->config.reset_ready_ms = zssc_value_or_default(
        device->config.reset_ready_ms, ZSSC3241_DEFAULT_RESET_READY_MS);

    device->address_7bit = address_7bit;
    device->state = ZSSC3241_STATE_IDLE;
    device->mode = ZSSC3241_MODE_UNKNOWN;
    device->last_error = ZSSC3241_OK;
    device->initialized = true;
    return ZSSC3241_OK;
}

Zssc3241Status ZSSC3241_Probe(Zssc3241 *device)
{
    Zssc3241DeviceStatus status;
    Zssc3241Status result = ZSSC3241_ReadStatus(device, &status);
    if (result == ZSSC3241_OK && status.busy) {
        return ZSSC3241_BUSY;
    }
    return result;
}

Zssc3241Status ZSSC3241_Reset(Zssc3241 *device)
{
    if (!zssc_device_valid(device)) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }
    if (device->transport.set_reset == NULL) {
        return ZSSC3241_UNSUPPORTED;
    }

    Zssc3241Status status = zssc_transport_status(
        device,
        device->transport.set_reset(
            device->transport.context, true));
    if (status != ZSSC3241_OK) {
        return status;
    }
    zssc_delay(device, device->config.reset_pulse_ms);

    status = zssc_transport_status(
        device,
        device->transport.set_reset(
            device->transport.context, false));
    if (status != ZSSC3241_OK) {
        return status;
    }
    zssc_delay(device, device->config.reset_ready_ms);

    device->state = ZSSC3241_STATE_IDLE;
    device->mode = ZSSC3241_MODE_UNKNOWN;
    device->eoc_pending = false;
    device->nvm_write_unlocked = false;
    device->result_pending = false;
    return ZSSC3241_Probe(device);
}

Zssc3241Status ZSSC3241_ReadStatus(Zssc3241 *device,
                                    Zssc3241DeviceStatus *status)
{
    if (status == NULL) {
        return ZSSC3241_INVALID_ARG;
    }
    if (!zssc_device_valid(device)) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }
    return zssc_read_status_internal(device, status);
}

Zssc3241Status ZSSC3241_EnterCommandMode(Zssc3241 *device)
{
    if (!zssc_device_valid(device)) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }
    if (device->state == ZSSC3241_STATE_WAIT_READY) {
        return ZSSC3241_BUSY;
    }
    if (device->mode == ZSSC3241_MODE_COMMAND) {
        device->state = ZSSC3241_STATE_IDLE;
        return ZSSC3241_OK;
    }

    uint8_t response[1];
    Zssc3241Status status = zssc_execute_command(
        device, ZSSC3241_CMD_START_COMMAND_MODE,
        false, 0U, response, sizeof(response),
        device->config.measurement_timeout_ms, false);
    if (status == ZSSC3241_OK &&
        device->mode != ZSSC3241_MODE_COMMAND) {
        status = ZSSC3241_WRONG_MODE;
    }
    if (status == ZSSC3241_OK) {
        device->state = ZSSC3241_STATE_IDLE;
    }
    return status;
}

Zssc3241Status ZSSC3241_EnterSleepMode(Zssc3241 *device)
{
    if (!zssc_device_valid(device)) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }
    if (device->state == ZSSC3241_STATE_WAIT_READY) {
        return ZSSC3241_BUSY;
    }
    if (device->mode == ZSSC3241_MODE_SLEEP) {
        device->state = ZSSC3241_STATE_IDLE;
        return ZSSC3241_OK;
    }

    uint8_t response[1];
    Zssc3241Status status = zssc_execute_command(
        device, ZSSC3241_CMD_START_SLEEP,
        false, 0U, response, sizeof(response),
        device->config.measurement_timeout_ms, false);
    if (status == ZSSC3241_OK && device->mode != ZSSC3241_MODE_SLEEP) {
        status = ZSSC3241_WRONG_MODE;
    }
    if (status == ZSSC3241_OK) {
        device->state = ZSSC3241_STATE_IDLE;
    }
    return status;
}

Zssc3241Status ZSSC3241_StartCyclicMode(Zssc3241 *device)
{
    if (!zssc_device_valid(device)) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }
    if (device->state == ZSSC3241_STATE_WAIT_READY) {
        return ZSSC3241_BUSY;
    }
    if (device->mode == ZSSC3241_MODE_CYCLIC) {
        device->state = ZSSC3241_STATE_CYCLIC;
        return ZSSC3241_OK;
    }

    uint8_t response[1];
    Zssc3241Status status = zssc_execute_command(
        device, ZSSC3241_CMD_START_CYCLIC,
        false, 0U, response, sizeof(response),
        device->config.measurement_timeout_ms, false);
    if (status == ZSSC3241_OK && device->mode != ZSSC3241_MODE_CYCLIC) {
        status = ZSSC3241_WRONG_MODE;
    }
    if (status == ZSSC3241_OK) {
        device->state = ZSSC3241_STATE_CYCLIC;
        device->eoc_pending = false;
    }
    return status;
}

Zssc3241Status ZSSC3241_StopCyclicMode(Zssc3241 *device)
{
    return ZSSC3241_EnterCommandMode(device);
}

Zssc3241Status ZSSC3241_StartMeasurement(
    Zssc3241 *device, Zssc3241MeasurementType type)
{
    Zssc3241Status status = zssc_require_non_cyclic(device);
    if (status != ZSSC3241_OK) {
        return status;
    }

    uint8_t command;
    uint8_t response_length;
    uint8_t sample_count;
    if (!zssc_measurement_parameters(
            type, &command, &response_length, &sample_count)) {
        return ZSSC3241_INVALID_ARG;
    }

    status = zssc_send_command(device, command, false, 0U);
    if (status != ZSSC3241_OK) {
        device->state = ZSSC3241_STATE_ERROR;
        device->last_error = status;
        return status;
    }

    device->pending_measurement = type;
    device->pending_command = command;
    device->pending_response_length = response_length;
    device->operation_start_ms = zssc_now(device);
    if (device->config.measurement_timeout_ms > UINT32_MAX / sample_count) {
        device->operation_timeout_ms = UINT32_MAX;
    } else {
        device->operation_timeout_ms =
            device->config.measurement_timeout_ms * sample_count;
    }
    device->next_poll_ms = device->operation_start_ms;
    device->eoc_pending = false;
    device->last_error = ZSSC3241_BUSY;
    device->state = ZSSC3241_STATE_WAIT_READY;
    return ZSSC3241_OK;
}

Zssc3241Status ZSSC3241_Process(Zssc3241 *device)
{
    if (!zssc_device_valid(device)) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }

    if (device->state == ZSSC3241_STATE_CYCLIC) {
        if (!device->eoc_pending) {
            return ZSSC3241_OK;
        }
        device->eoc_pending = false;

        Zssc3241Measurement result;
        Zssc3241Status status = zssc_read_measurement_frame(
            device, ZSSC3241_MEASUREMENT_CYCLIC,
            ZSSC3241_MEASUREMENT_FRAME_BYTES, &result);
        if (status == ZSSC3241_OK ||
            status == ZSSC3241_MEMORY_ERROR ||
            status == ZSSC3241_CONNECTION_FAULT ||
            status == ZSSC3241_MATH_SATURATION) {
            zssc_publish_result(device, &result);
        }
        device->last_error = status;
        return status;
    }

    if (device->state != ZSSC3241_STATE_WAIT_READY) {
        return device->state == ZSSC3241_STATE_TIMEOUT
                   ? ZSSC3241_TIMEOUT
                   : device->state == ZSSC3241_STATE_ERROR
                         ? device->last_error : ZSSC3241_OK;
    }

    const uint32_t now = zssc_now(device);
    if (zssc_elapsed(now, device->operation_start_ms,
                     device->operation_timeout_ms)) {
        device->state = ZSSC3241_STATE_TIMEOUT;
        device->last_error = ZSSC3241_TIMEOUT;
        device->timeout_count++;
        return ZSSC3241_TIMEOUT;
    }

    if (!device->eoc_pending && !zssc_time_reached(now, device->next_poll_ms)) {
        return ZSSC3241_BUSY;
    }
    device->eoc_pending = false;

    Zssc3241DeviceStatus status_byte;
    Zssc3241Status status = zssc_read_status_internal(device, &status_byte);
    if (status != ZSSC3241_OK) {
        device->state = ZSSC3241_STATE_ERROR;
        device->last_error = status;
        return status;
    }
    if (status_byte.busy) {
        device->next_poll_ms = now + device->config.poll_interval_ms;
        return ZSSC3241_BUSY;
    }

    Zssc3241Measurement result;
    status = zssc_read_measurement_frame(
        device, device->pending_measurement,
        device->pending_response_length, &result);
    if (status == ZSSC3241_OK ||
        status == ZSSC3241_MEMORY_ERROR ||
        status == ZSSC3241_CONNECTION_FAULT ||
        status == ZSSC3241_MATH_SATURATION) {
        zssc_publish_result(device, &result);
    }

    device->state = ZSSC3241_STATE_IDLE;
    device->last_error = status;
    if (!device->config.reject_faulty_measurements &&
        (status == ZSSC3241_MEMORY_ERROR ||
         status == ZSSC3241_CONNECTION_FAULT ||
         status == ZSSC3241_MATH_SATURATION)) {
        return ZSSC3241_OK;
    }
    return status;
}

void ZSSC3241_OnEocInterrupt(Zssc3241 *device)
{
    if (device == NULL || !device->initialized) {
        return;
    }
    if (!device->config.use_eoc_interrupt) {
        device->unexpected_eoc_count++;
        return;
    }
    device->eoc_count++;
    if (device->state != ZSSC3241_STATE_WAIT_READY &&
        device->state != ZSSC3241_STATE_CYCLIC) {
        device->unexpected_eoc_count++;
    }
    device->eoc_pending = true;
}

void ZSSC3241_Cancel(Zssc3241 *device)
{
    if (device == NULL || !device->initialized) {
        return;
    }
    if (device->state == ZSSC3241_STATE_WAIT_READY ||
        device->state == ZSSC3241_STATE_TIMEOUT ||
        device->state == ZSSC3241_STATE_ERROR) {
        device->state = device->mode == ZSSC3241_MODE_CYCLIC
            ? ZSSC3241_STATE_CYCLIC : ZSSC3241_STATE_IDLE;
    }
    device->eoc_pending = false;
    device->last_error = ZSSC3241_OK;
}

bool ZSSC3241_IsBusy(const Zssc3241 *device)
{
    return device != NULL &&
           device->state == ZSSC3241_STATE_WAIT_READY;
}

Zssc3241State ZSSC3241_GetState(const Zssc3241 *device)
{
    return device != NULL ? device->state : ZSSC3241_STATE_UNINITIALIZED;
}

bool ZSSC3241_HasResult(const Zssc3241 *device)
{
    return device != NULL && device->result_pending;
}

Zssc3241Status ZSSC3241_GetLatestResult(
    Zssc3241 *device, Zssc3241Measurement *result)
{
    if (device == NULL || result == NULL) {
        return ZSSC3241_INVALID_ARG;
    }
    if (!device->initialized) {
        return ZSSC3241_NOT_INITIALIZED;
    }
    if (!device->result_pending) {
        return ZSSC3241_NO_RESULT;
    }

    *result = device->result;
    device->result_pending = false;
    return result->operation_status;
}

Zssc3241Status ZSSC3241_Measure(
    Zssc3241 *device, Zssc3241Measurement *result)
{
    return zssc_measure_blocking(
        device, ZSSC3241_MEASUREMENT_CORRECTED, result);
}

Zssc3241Status ZSSC3241_MeasureRawSensor(
    Zssc3241 *device, Zssc3241Measurement *result)
{
    return zssc_measure_blocking(
        device, ZSSC3241_MEASUREMENT_RAW_SENSOR, result);
}

Zssc3241Status ZSSC3241_MeasureRawTemperature(
    Zssc3241 *device, Zssc3241Measurement *result)
{
    return zssc_measure_blocking(
        device, ZSSC3241_MEASUREMENT_RAW_TEMPERATURE, result);
}

Zssc3241Status ZSSC3241_MeasureOversampled(
    Zssc3241 *device, uint8_t sample_count, Zssc3241Measurement *result)
{
    Zssc3241MeasurementType type;
    switch (sample_count) {
    case 2U:
        type = ZSSC3241_MEASUREMENT_OVERSAMPLE_2;
        break;
    case 4U:
        type = ZSSC3241_MEASUREMENT_OVERSAMPLE_4;
        break;
    case 8U:
        type = ZSSC3241_MEASUREMENT_OVERSAMPLE_8;
        break;
    case 16U:
        type = ZSSC3241_MEASUREMENT_OVERSAMPLE_16;
        break;
    default:
        return ZSSC3241_INVALID_ARG;
    }
    return zssc_measure_blocking(device, type, result);
}

Zssc3241Status ZSSC3241_ReadCyclicResult(
    Zssc3241 *device, Zssc3241Measurement *result)
{
    if (!zssc_device_valid(device) || result == NULL) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }
    if (device->mode != ZSSC3241_MODE_CYCLIC ||
        device->state != ZSSC3241_STATE_CYCLIC) {
        return ZSSC3241_WRONG_MODE;
    }
    return zssc_read_measurement_frame(
        device, ZSSC3241_MEASUREMENT_CYCLIC,
        ZSSC3241_MEASUREMENT_FRAME_BYTES, result);
}

Zssc3241Status ZSSC3241_ReadDiagnostics(
    Zssc3241 *device, Zssc3241Diagnostics *diagnostics)
{
    if (diagnostics == NULL) {
        return ZSSC3241_INVALID_ARG;
    }
    Zssc3241Status status = zssc_require_non_cyclic(device);
    if (status != ZSSC3241_OK) {
        return status;
    }

    uint8_t response[ZSSC3241_WORD_FRAME_BYTES];
    status = zssc_execute_command(
        device, ZSSC3241_CMD_CHECK_DIAG,
        false, 0U, response, sizeof(response),
        device->config.diagnostic_timeout_ms, false);
    if (status != ZSSC3241_OK) {
        return status;
    }

    memset(diagnostics, 0, sizeof(*diagnostics));
    zssc_decode_status_byte(response[0], &diagnostics->device_status);
    diagnostics->raw = (uint16_t)(((uint16_t)response[1] << 8) |
                                  response[2]);
    diagnostics->valid = true;
    status = zssc_fault_status(&diagnostics->device_status);
    if (status != ZSSC3241_OK) {
        return status;
    }
    if ((diagnostics->raw & ZSSC3241_DIAG_MEMORY_ERROR) != 0U) {
        return ZSSC3241_MEMORY_ERROR;
    }
    if ((diagnostics->raw & ZSSC3241_DIAG_MATH_SATURATION) != 0U) {
        return ZSSC3241_MATH_SATURATION;
    }
    return (diagnostics->raw & ZSSC3241_DIAG_FAULT_MASK) != 0U
               ? ZSSC3241_CONNECTION_FAULT : ZSSC3241_OK;
}

Zssc3241Status ZSSC3241_ResetDiagnostics(Zssc3241 *device)
{
    Zssc3241Status status = zssc_require_non_cyclic(device);
    if (status != ZSSC3241_OK) {
        return status;
    }
    uint8_t response[1];
    return zssc_execute_command(
        device, ZSSC3241_CMD_RESET_DIAG,
        false, 0U, response, sizeof(response),
        device->config.diagnostic_timeout_ms, false);
}

Zssc3241Status ZSSC3241_UpdateDiagnostics(Zssc3241 *device)
{
    Zssc3241Status status = zssc_require_non_cyclic(device);
    if (status != ZSSC3241_OK) {
        return status;
    }
    uint8_t response[1];
    return zssc_execute_command(
        device, ZSSC3241_CMD_UPDATE_DIAG,
        false, 0U, response, sizeof(response),
        device->config.diagnostic_timeout_ms, false);
}

Zssc3241Status ZSSC3241_SetDacDiagnostic(
    Zssc3241 *device, uint16_t dac_value)
{
    Zssc3241Status status = zssc_require_command_mode(device);
    if (status != ZSSC3241_OK) {
        return status;
    }
    uint8_t response[1];
    return zssc_execute_command(
        device, ZSSC3241_CMD_DAC_DIAG,
        true, dac_value, response, sizeof(response),
        device->config.diagnostic_timeout_ms, false);
}

Zssc3241Status ZSSC3241_RunSelfDiagnostic(
    Zssc3241 *device, uint8_t pseudo_offset,
    Zssc3241Measurement *result)
{
    if (result == NULL) {
        return ZSSC3241_INVALID_ARG;
    }
    Zssc3241Status status = zssc_require_command_mode(device);
    if (status != ZSSC3241_OK) {
        return status;
    }

    uint8_t response[ZSSC3241_RAW_FRAME_BYTES];
    status = zssc_execute_command(
        device, ZSSC3241_CMD_SELF_DIAG_MEASURE,
        true, pseudo_offset, response, sizeof(response),
        device->config.diagnostic_timeout_ms, false);
    if (status != ZSSC3241_OK) {
        return status;
    }
    return zssc_decode_measurement(
        device, ZSSC3241_MEASUREMENT_SELF_DIAGNOSTIC,
        response, sizeof(response), result);
}

Zssc3241Status ZSSC3241_ReadNvm(
    Zssc3241 *device, uint8_t address, uint16_t *value)
{
    if (value == NULL || address > ZSSC3241_NVM_READ_MAX) {
        return ZSSC3241_INVALID_ARG;
    }
    Zssc3241Status status = zssc_require_non_cyclic(device);
    if (status != ZSSC3241_OK) {
        return status;
    }

    uint8_t response[ZSSC3241_WORD_FRAME_BYTES];
    status = zssc_execute_command(
        device, address, false, 0U,
        response, sizeof(response),
        device->config.nvm_timeout_ms, true);
    if (status != ZSSC3241_OK) {
        return status;
    }
    *value = (uint16_t)(((uint16_t)response[1] << 8) | response[2]);
    return ZSSC3241_OK;
}

Zssc3241Status ZSSC3241_DumpNvm(
    Zssc3241 *device, uint16_t *buffer, size_t word_count)
{
    if (buffer == NULL || word_count < ZSSC3241_NVM_WORD_COUNT) {
        return ZSSC3241_INVALID_ARG;
    }
    for (uint8_t address = ZSSC3241_NVM_READ_MIN;
         address <= ZSSC3241_NVM_READ_MAX; ++address) {
        Zssc3241Status status = ZSSC3241_ReadNvm(
            device, address, &buffer[address]);
        if (status != ZSSC3241_OK) {
            return status;
        }
    }
    return ZSSC3241_OK;
}

Zssc3241Status ZSSC3241_UnlockNvmWrites(
    Zssc3241 *device, uint32_t unlock_key)
{
    if (!zssc_device_valid(device)) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }
    if (!device->config.allow_nvm_write) {
        return ZSSC3241_NVM_WRITE_DISABLED;
    }
    if (unlock_key != ZSSC3241_NVM_UNLOCK_KEY) {
        return ZSSC3241_INVALID_ARG;
    }
    device->nvm_write_unlocked = true;
    return ZSSC3241_OK;
}

void ZSSC3241_LockNvmWrites(Zssc3241 *device)
{
    if (device != NULL) {
        device->nvm_write_unlocked = false;
    }
}

Zssc3241Status ZSSC3241_WriteNvm(
    Zssc3241 *device, uint8_t address, uint16_t value)
{
    if (!zssc_device_valid(device)) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }
    if (!device->config.allow_nvm_write ||
        !device->nvm_write_unlocked) {
        return ZSSC3241_NVM_WRITE_DISABLED;
    }
    if (address > ZSSC3241_NVM_CUSTOM_WRITE_MAX) {
        return ZSSC3241_OUT_OF_RANGE;
    }

    uint16_t current;
    Zssc3241Status status = ZSSC3241_ReadNvm(device, address, &current);
    if (status != ZSSC3241_OK) {
        return status;
    }
    if (address == ZSSC3241_NVM_SSF1) {
        if ((current & ZSSC3241_NVM_LOCK_MASK) != 0U) {
            return ZSSC3241_NVM_LOCKED;
        }
        if ((value & ZSSC3241_NVM_LOCK_MASK) != 0U) {
            return ZSSC3241_NVM_LOCK_PROTECTED;
        }
    }
    if (current == value) {
        return ZSSC3241_OK;
    }

    uint8_t response[1];
    status = zssc_execute_command(
        device, (uint8_t)(ZSSC3241_CMD_NVM_WRITE_BASE + address),
        true, value, response, sizeof(response),
        device->config.nvm_timeout_ms, true);
    if (status != ZSSC3241_OK) {
        return status;
    }
    device->nvm_write_count++;

    if (device->config.verify_nvm_after_write) {
        uint16_t readback;
        status = ZSSC3241_ReadNvm(device, address, &readback);
        if (status != ZSSC3241_OK) {
            return status;
        }
        if (readback != value) {
            device->nvm_verify_failure_count++;
            return ZSSC3241_NVM_VERIFY_FAILED;
        }
    }
    return ZSSC3241_OK;
}

Zssc3241Status ZSSC3241_WriteNvmBlock(
    Zssc3241 *device, uint8_t start_address,
    const uint16_t *values, size_t word_count,
    bool update_checksum)
{
    if (values == NULL || word_count == 0U ||
        start_address > ZSSC3241_NVM_CUSTOM_WRITE_MAX ||
        word_count > (size_t)(ZSSC3241_NVM_CUSTOM_WRITE_MAX -
                              start_address + 1U)) {
        return ZSSC3241_INVALID_ARG;
    }

    for (size_t i = 0U; i < word_count; ++i) {
        Zssc3241Status status = ZSSC3241_WriteNvm(
            device, (uint8_t)(start_address + i), values[i]);
        if (status != ZSSC3241_OK) {
            return status;
        }
    }
    if (!update_checksum) {
        return ZSSC3241_OK;
    }

    Zssc3241Status status = ZSSC3241_UpdateNvmChecksum(device);
    if (status != ZSSC3241_OK) {
        return status;
    }
    status = ZSSC3241_UpdateDiagnostics(device);
    if (status != ZSSC3241_OK) {
        return status;
    }
    Zssc3241Diagnostics diagnostics;
    return ZSSC3241_ReadDiagnostics(device, &diagnostics);
}

Zssc3241Status ZSSC3241_UpdateNvmChecksum(Zssc3241 *device)
{
    if (!zssc_device_valid(device)) {
        return device != NULL && !device->initialized
                   ? ZSSC3241_NOT_INITIALIZED
                   : ZSSC3241_INVALID_ARG;
    }
    if (!device->config.allow_nvm_write ||
        !device->nvm_write_unlocked) {
        return ZSSC3241_NVM_WRITE_DISABLED;
    }
    Zssc3241Status status = zssc_require_non_cyclic(device);
    if (status != ZSSC3241_OK) {
        return status;
    }

    uint8_t response[1];
    return zssc_execute_command(
        device, ZSSC3241_CMD_CALCULATE_NVM_CHECKSUM,
        false, 0U, response, sizeof(response),
        device->config.nvm_timeout_ms, false);
}

Zssc3241Status ZSSC3241_OverwriteShadow(
    Zssc3241 *device, Zssc3241ShadowRegister reg, uint16_t value)
{
    if ((uint8_t)reg < ZSSC3241_CMD_OVERWRITE_SM_CONFIG1 ||
        (uint8_t)reg > ZSSC3241_CMD_OVERWRITE_SSF2) {
        return ZSSC3241_INVALID_ARG;
    }
    Zssc3241Status status = zssc_require_command_mode(device);
    if (status != ZSSC3241_OK) {
        return status;
    }

    uint8_t response[1];
    return zssc_execute_command(
        device, (uint8_t)reg, true, value,
        response, sizeof(response),
        device->config.measurement_timeout_ms, false);
}

Zssc3241Status ZSSC3241_SetPostCalibrationOffset(
    Zssc3241 *device, uint16_t expected_output)
{
    Zssc3241Status status = zssc_require_non_cyclic(device);
    if (status != ZSSC3241_OK) {
        return status;
    }
    uint8_t response[1];
    return zssc_execute_command(
        device, ZSSC3241_CMD_POST_CAL_OFFSET,
        true, expected_output, response, sizeof(response),
        device->config.measurement_timeout_ms, false);
}

int32_t ZSSC3241_DecodeSigned24(uint32_t raw24)
{
    raw24 &= UINT32_C(0x00FFFFFF);
    if ((raw24 & UINT32_C(0x00800000)) != 0U) {
        raw24 |= UINT32_C(0xFF000000);
    }
    return (int32_t)raw24;
}

uint32_t ZSSC3241_DecodeUnsigned24(const uint8_t data[3])
{
    if (data == NULL) {
        return 0U;
    }
    return ((uint32_t)data[0] << 16) |
           ((uint32_t)data[1] << 8) |
           data[2];
}

float ZSSC3241_CorrectedToNormalized(uint32_t corrected_raw24)
{
    return (float)(corrected_raw24 & UINT32_C(0x00FFFFFF)) /
           8388608.0f;
}

float ZSSC3241_RawToNormalized(int32_t signed_raw24)
{
    return (float)signed_raw24 / 8388608.0f;
}

Zssc3241Status ZSSC3241_MapCorrected(
    uint32_t corrected_raw24,
    uint32_t code_min, uint32_t code_max,
    int32_t physical_min, int32_t physical_max,
    int32_t *physical_value)
{
    if (physical_value == NULL || code_max <= code_min ||
        code_max > UINT32_C(0x00FFFFFF) ||
        corrected_raw24 < code_min || corrected_raw24 > code_max) {
        return physical_value == NULL || code_max <= code_min
                   ? ZSSC3241_INVALID_ARG : ZSSC3241_OUT_OF_RANGE;
    }

    const int64_t code_span = (int64_t)code_max - code_min;
    const int64_t physical_span =
        (int64_t)physical_max - (int64_t)physical_min;
    int64_t numerator =
        ((int64_t)corrected_raw24 - code_min) * physical_span;

    if (numerator >= 0) {
        numerator += code_span / 2;
    } else {
        numerator -= code_span / 2;
    }
    const int64_t mapped = (int64_t)physical_min + numerator / code_span;
    if (mapped < INT32_MIN || mapped > INT32_MAX) {
        return ZSSC3241_OUT_OF_RANGE;
    }
    *physical_value = (int32_t)mapped;
    return ZSSC3241_OK;
}

const char *ZSSC3241_StatusString(Zssc3241Status status)
{
    switch (status) {
    case ZSSC3241_OK: return "OK";
    case ZSSC3241_BUSY: return "BUSY";
    case ZSSC3241_TIMEOUT: return "TIMEOUT";
    case ZSSC3241_INVALID_ARG: return "INVALID_ARG";
    case ZSSC3241_NOT_INITIALIZED: return "NOT_INITIALIZED";
    case ZSSC3241_I2C_ERROR: return "I2C_ERROR";
    case ZSSC3241_NACK: return "NACK";
    case ZSSC3241_NOT_READY: return "NOT_READY";
    case ZSSC3241_INVALID_RESPONSE: return "INVALID_RESPONSE";
    case ZSSC3241_WRONG_MODE: return "WRONG_MODE";
    case ZSSC3241_MEMORY_ERROR: return "MEMORY_ERROR";
    case ZSSC3241_CONNECTION_FAULT: return "CONNECTION_FAULT";
    case ZSSC3241_MATH_SATURATION: return "MATH_SATURATION";
    case ZSSC3241_NVM_WRITE_DISABLED: return "NVM_WRITE_DISABLED";
    case ZSSC3241_NVM_LOCKED: return "NVM_LOCKED";
    case ZSSC3241_NVM_LOCK_PROTECTED: return "NVM_LOCK_PROTECTED";
    case ZSSC3241_NVM_VERIFY_FAILED: return "NVM_VERIFY_FAILED";
    case ZSSC3241_NO_RESULT: return "NO_RESULT";
    case ZSSC3241_STALE_RESULT: return "STALE_RESULT";
    case ZSSC3241_UNSUPPORTED: return "UNSUPPORTED";
    case ZSSC3241_OUT_OF_RANGE: return "OUT_OF_RANGE";
    default: return "UNKNOWN";
    }
}
