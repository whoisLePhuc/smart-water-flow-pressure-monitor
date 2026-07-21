/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : fram_driver.c
  * @brief          : FM24CL04B FRAM driver — STM32 HAL I2C implementation
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "fram_driver.h"
#include "main.h"            /* hi2c1 */

/* External variables --------------------------------------------------------*/
/* hi2c1 is declared in main.c */

/* Private helpers -----------------------------------------------------------*/

/**
  * @brief  Resolve slave address (7-bit) and page offset from a logical
  *         address (0..511).
  * @param  address    Logical byte address
  * @param  out_slave  [out] 7-bit I2C slave address
  * @param  out_offset [out] Byte offset within the selected page (0..255)
  */
static void fram_resolve_addr(uint16_t address,
                              uint8_t *out_slave,
                              uint8_t *out_offset)
{
    if (address < FRAM_PAGE_SIZE) {
        *out_slave  = FRAM_SLAVE_ADDR_P0;
        *out_offset = (uint8_t)address;
    } else {
        *out_slave  = FRAM_SLAVE_ADDR_P1;
        *out_offset = (uint8_t)(address - FRAM_PAGE_SIZE);
    }
}

/* Public API ----------------------------------------------------------------*/

FramStatus FRAM_Init(void)
{
    /* I2C1 is already initialised by CubeMX MX_I2C1_Init(). */
    return FRAM_OK;
}

bool FRAM_Probe(void)
{
    HAL_StatusTypeDef hal;
    /* Check both pages */
    hal = HAL_I2C_IsDeviceReady(&hi2c1,
                                FRAM_SLAVE_ADDR_P0 << 1,
                                3, FRAM_TIMEOUT_MS);
    if (hal != HAL_OK) return false;

    hal = HAL_I2C_IsDeviceReady(&hi2c1,
                                FRAM_SLAVE_ADDR_P1 << 1,
                                3, FRAM_TIMEOUT_MS);
    return (hal == HAL_OK);
}

FramStatus FRAM_Read(uint16_t address, uint8_t *buffer, uint16_t length)
{
    /* Parameter validation */
    if (!buffer)
        return FRAM_INVALID_PARAM;

    if (address >= FRAM_SIZE_BYTES)
        return FRAM_OUT_OF_RANGE;

    if (length == 0u)
        return FRAM_OK;

    if ((uint32_t)address + (uint32_t)length > FRAM_SIZE_BYTES)
        return FRAM_OUT_OF_RANGE;

    /* Resolve slave address and page offset */
    uint8_t slave, offset;
    fram_resolve_addr(address, &slave, &offset);

    /* Single-page read: check that range doesn't cross a page boundary */
    if (address / FRAM_PAGE_SIZE == (address + length - 1u) / FRAM_PAGE_SIZE) {
        HAL_StatusTypeDef hal = HAL_I2C_Mem_Read(
            &hi2c1,
            (uint16_t)slave << 1,
            (uint16_t)offset,
            I2C_MEMADD_SIZE_8BIT,
            buffer,
            length,
            FRAM_TIMEOUT_MS);
        return (hal == HAL_OK) ? FRAM_OK : FRAM_IO_ERROR;
    }

    /* Cross-page read: split into two transactions */
    uint16_t first_chunk  = (uint16_t)(FRAM_PAGE_SIZE - address);
    uint16_t second_chunk = length - first_chunk;

    HAL_StatusTypeDef hal;

    hal = HAL_I2C_Mem_Read(&hi2c1,
                           (uint16_t)slave << 1,
                           (uint16_t)offset,
                           I2C_MEMADD_SIZE_8BIT,
                           buffer,
                           first_chunk,
                           FRAM_TIMEOUT_MS);
    if (hal != HAL_OK)
        return FRAM_IO_ERROR;

    /* Second page: slave = FRAM_SLAVE_ADDR_P1, offset = 0 */
    hal = HAL_I2C_Mem_Read(&hi2c1,
                           (uint16_t)FRAM_SLAVE_ADDR_P1 << 1,
                           0u,
                           I2C_MEMADD_SIZE_8BIT,
                           buffer + first_chunk,
                           second_chunk,
                           FRAM_TIMEOUT_MS);
    return (hal == HAL_OK) ? FRAM_OK : FRAM_IO_ERROR;
}

FramStatus FRAM_Write(uint16_t address, const uint8_t *data, uint16_t length)
{
    /* Parameter validation */
    if (!data)
        return FRAM_INVALID_PARAM;

    if (address >= FRAM_SIZE_BYTES)
        return FRAM_OUT_OF_RANGE;

    if (length == 0u)
        return FRAM_OK;

    if ((uint32_t)address + (uint32_t)length > FRAM_SIZE_BYTES)
        return FRAM_OUT_OF_RANGE;

    /* Resolve slave address and page offset */
    uint8_t slave, offset;
    fram_resolve_addr(address, &slave, &offset);

    /* Single-page write: check that range doesn't cross a page boundary */
    if (address / FRAM_PAGE_SIZE == (address + length - 1u) / FRAM_PAGE_SIZE) {
        HAL_StatusTypeDef hal = HAL_I2C_Mem_Write(
            &hi2c1,
            (uint16_t)slave << 1,
            (uint16_t)offset,
            I2C_MEMADD_SIZE_8BIT,
            (uint8_t *)data,       /* HAL param is uint8_t*, content is const */
            length,
            FRAM_TIMEOUT_MS);
        return (hal == HAL_OK) ? FRAM_OK : FRAM_IO_ERROR;
    }

    /* Cross-page write: split into two transactions */
    uint16_t first_chunk  = (uint16_t)(FRAM_PAGE_SIZE - address);
    uint16_t second_chunk = length - first_chunk;

    HAL_StatusTypeDef hal;

    hal = HAL_I2C_Mem_Write(&hi2c1,
                            (uint16_t)slave << 1,
                            (uint16_t)offset,
                            I2C_MEMADD_SIZE_8BIT,
                            (uint8_t *)data,
                            first_chunk,
                            FRAM_TIMEOUT_MS);
    if (hal != HAL_OK)
        return FRAM_IO_ERROR;

    /* Second page: slave = FRAM_SLAVE_ADDR_P1, offset = 0 */
    hal = HAL_I2C_Mem_Write(&hi2c1,
                            (uint16_t)FRAM_SLAVE_ADDR_P1 << 1,
                            0u,
                            I2C_MEMADD_SIZE_8BIT,
                            (uint8_t *)(data + first_chunk),
                            second_chunk,
                            FRAM_TIMEOUT_MS);
    return (hal == HAL_OK) ? FRAM_OK : FRAM_IO_ERROR;
}
