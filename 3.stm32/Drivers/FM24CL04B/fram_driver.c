/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : fram_driver.c
  * @brief          : FM24CL04B F-RAM driver using blocking STM32 HAL I2C
  ******************************************************************************
  */
/* USER CODE END Header */

#include "fram_driver.h"
#include "main.h"
#include <stddef.h>

/*
 * Override at compile time, for example with
 * -DFRAM_I2C_HANDLE=hi2c2, when a different peripheral is used.
 */
#ifndef FRAM_I2C_HANDLE
#define FRAM_I2C_HANDLE hi2c1
#endif

/* The handle is defined by CubeMX-generated application code. */
extern I2C_HandleTypeDef FRAM_I2C_HANDLE;

/* Private helpers -----------------------------------------------------------*/

static FramStatus fram_status_from_hal(HAL_StatusTypeDef hal_status)
{
    switch (hal_status) {
    case HAL_OK:
        return FRAM_OK;

    case HAL_TIMEOUT:
        return FRAM_TIMEOUT;

    case HAL_BUSY:
        return FRAM_BUSY;

    case HAL_ERROR:
    default:
        return FRAM_IO_ERROR;
    }
}

static FramStatus fram_validate_transfer(uint16_t address,
                                         const void *buffer,
                                         uint16_t length)
{
    if (length == 0u) {
        return (address < FRAM_SIZE_BYTES) ? FRAM_OK : FRAM_OUT_OF_RANGE;
    }

    if (buffer == NULL) {
        return FRAM_INVALID_PARAM;
    }

    if (address >= FRAM_SIZE_BYTES) {
        return FRAM_OUT_OF_RANGE;
    }

    /* Subtraction is safe after validating address and avoids addition
     * overflow if the address type is widened in a future revision. */
    if (length > (uint16_t)(FRAM_SIZE_BYTES - address)) {
        return FRAM_OUT_OF_RANGE;
    }

    return FRAM_OK;
}

static void fram_resolve_address(uint16_t address,
                                 uint8_t *slave_address,
                                 uint8_t *word_address)
{
    *slave_address = ((address & 0x0100u) != 0u)
                         ? (uint8_t)FRAM_SLAVE_ADDR_P1
                         : (uint8_t)FRAM_SLAVE_ADDR_P0;
    *word_address = (uint8_t)(address & 0x00FFu);
}

static FramStatus fram_write_status_from_hal(HAL_StatusTypeDef hal_status,
                                             uint8_t slave_address)
{
    if (hal_status != HAL_ERROR) {
        return fram_status_from_hal(hal_status);
    }

    /* With WP high, FM24CL04B acknowledges its slave/word address but NACKs
     * the data byte. STM32 HAL reports this as HAL_I2C_ERROR_AF. Probe the
     * address once to distinguish this case from an absent device. */
    if ((HAL_I2C_GetError(&FRAM_I2C_HANDLE) & HAL_I2C_ERROR_AF) != 0u) {
        HAL_StatusTypeDef probe_status = HAL_I2C_IsDeviceReady(
            &FRAM_I2C_HANDLE,
            (uint16_t)slave_address << 1u,
            1u,
            FRAM_TIMEOUT_MS);

        if (probe_status == HAL_OK) {
            return FRAM_WRITE_PROTECTED;
        }
    }

    return FRAM_IO_ERROR;
}

/* Public API ----------------------------------------------------------------*/

FramStatus FRAM_Init(void)
{
    return FRAM_ProbeStatus();
}

FramStatus FRAM_ProbeStatus(void)
{
    static const uint8_t slave_addresses[] = {
        (uint8_t)FRAM_SLAVE_ADDR_P0,
        (uint8_t)FRAM_SLAVE_ADDR_P1
    };

    for (uint32_t i = 0u;
         i < (uint32_t)(sizeof(slave_addresses) / sizeof(slave_addresses[0]));
         ++i) {
        HAL_StatusTypeDef hal_status = HAL_I2C_IsDeviceReady(
            &FRAM_I2C_HANDLE,
            (uint16_t)slave_addresses[i] << 1u,
            FRAM_READY_TRIALS,
            FRAM_TIMEOUT_MS);

        if (hal_status != HAL_OK) {
            return fram_status_from_hal(hal_status);
        }
    }

    return FRAM_OK;
}

bool FRAM_Probe(void)
{
    return FRAM_ProbeStatus() == FRAM_OK;
}

FramStatus FRAM_Read(uint16_t address, uint8_t *buffer, uint16_t length)
{
    FramStatus status = fram_validate_transfer(address, buffer, length);
    if (status != FRAM_OK || length == 0u) {
        return status;
    }

    uint16_t current_address = address;
    uint16_t remaining = length;
    uint8_t *destination = buffer;

    while (remaining > 0u) {
        uint8_t slave_address;
        uint8_t word_address;
        fram_resolve_address(current_address, &slave_address, &word_address);

        uint16_t chunk = (uint16_t)(FRAM_ADDRESS_BLOCK_SIZE - word_address);
        if (chunk > remaining) {
            chunk = remaining;
        }

        HAL_StatusTypeDef hal_status = HAL_I2C_Mem_Read(
            &FRAM_I2C_HANDLE,
            (uint16_t)slave_address << 1u,
            word_address,
            I2C_MEMADD_SIZE_8BIT,
            destination,
            chunk,
            FRAM_TIMEOUT_MS);

        status = fram_status_from_hal(hal_status);
        if (status != FRAM_OK) {
            return status;
        }

        current_address = (uint16_t)(current_address + chunk);
        destination += chunk;
        remaining = (uint16_t)(remaining - chunk);
    }

    return FRAM_OK;
}

FramStatus FRAM_Write(uint16_t address, const uint8_t *data, uint16_t length)
{
    FramStatus status = fram_validate_transfer(address, data, length);
    if (status != FRAM_OK || length == 0u) {
        return status;
    }

    uint16_t current_address = address;
    uint16_t remaining = length;
    const uint8_t *source = data;

    while (remaining > 0u) {
        uint8_t slave_address;
        uint8_t word_address;
        fram_resolve_address(current_address, &slave_address, &word_address);

        uint16_t chunk = (uint16_t)(FRAM_ADDRESS_BLOCK_SIZE - word_address);
        if (chunk > remaining) {
            chunk = remaining;
        }

        HAL_StatusTypeDef hal_status = HAL_I2C_Mem_Write(
            &FRAM_I2C_HANDLE,
            (uint16_t)slave_address << 1u,
            word_address,
            I2C_MEMADD_SIZE_8BIT,
            (uint8_t *)(uintptr_t)source,
            chunk,
            FRAM_TIMEOUT_MS);

        status = fram_write_status_from_hal(hal_status, slave_address);
        if (status != FRAM_OK) {
            return status;
        }

        current_address = (uint16_t)(current_address + chunk);
        source += chunk;
        remaining = (uint16_t)(remaining - chunk);
    }

    return FRAM_OK;
}