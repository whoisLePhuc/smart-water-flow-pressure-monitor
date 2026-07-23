/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : fram_driver.h
  * @brief          : FM24CL04B F-RAM driver over STM32 HAL I2C
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef FRAM_DRIVER_H_
#define FRAM_DRIVER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* FM24CL04B memory organization --------------------------------------------*/
#define FRAM_SIZE_BYTES              512u
#define FRAM_ADDRESS_BLOCK_SIZE      256u

/* Backward-compatible name. This is an address block selected by A8, not an
 * EEPROM-style write-buffer page. The FM24CL04B has no page buffer. */
#define FRAM_PAGE_SIZE               FRAM_ADDRESS_BLOCK_SIZE

/*
 * FM24CL04B 7-bit I2C address:
 *
 *     1 0 1 0 A2 A1 A8
 *
 * A2 and A1 are hardware device-select pins. A8 is bit 8 of the logical
 * memory address. The FM24CL04B has no external A0 pin.
 *
 * FRAM_DEVICE_SELECT encodes the pin levels as (A2 << 1) | A1:
 *   0: A2=0, A1=0 -> 0x50 / 0x51 (default)
 *   1: A2=0, A1=1 -> 0x52 / 0x53
 *   2: A2=1, A1=0 -> 0x54 / 0x55
 *   3: A2=1, A1=1 -> 0x56 / 0x57
 */
#ifndef FRAM_DEVICE_SELECT
#define FRAM_DEVICE_SELECT           0u
#endif

#if (FRAM_DEVICE_SELECT > 3u)
#error "FRAM_DEVICE_SELECT must be in the range 0..3"
#endif

#define FRAM_SLAVE_ADDR_P0 \
    (0x50u | ((FRAM_DEVICE_SELECT & 0x03u) << 1u))
#define FRAM_SLAVE_ADDR_P1           (FRAM_SLAVE_ADDR_P0 | 0x01u)

#ifndef FRAM_TIMEOUT_MS
#define FRAM_TIMEOUT_MS              100u
#endif

#ifndef FRAM_READY_TRIALS
#define FRAM_READY_TRIALS            3u
#endif

/* Status codes --------------------------------------------------------------*/
typedef enum {
    FRAM_OK = 0,
    FRAM_OUT_OF_RANGE,
    FRAM_IO_ERROR,
    FRAM_INVALID_PARAM,
    FRAM_WRITE_PROTECTED,
    FRAM_TIMEOUT,
    FRAM_BUSY
} FramStatus;

/* Public API ----------------------------------------------------------------*/

/**
  * @brief  Probe both FM24CL04B address blocks and initialise the driver.
  * @note   MX_I2C1_Init(), or the selected I2C peripheral initialiser, must be
  *         called before this function.
  * @retval FRAM_OK when both address blocks acknowledge; otherwise an error.
  */
FramStatus FRAM_Init(void);

/**
  * @brief  Read bytes from F-RAM using blocking STM32 HAL transfers.
  * @param  address Logical byte address in the range 0..511.
  * @param  buffer  Destination buffer. Must not be NULL when length > 0.
  * @param  length  Number of bytes to read. A zero length is a no-op.
  * @note   This blocking API must not be called from an interrupt. If multiple
  *         RTOS tasks share the I2C bus, protect the bus with a mutex.
  */
FramStatus FRAM_Read(uint16_t address, uint8_t *buffer, uint16_t length);

/**
  * @brief  Write bytes to F-RAM using blocking STM32 HAL transfers.
  * @param  address Logical byte address in the range 0..511.
  * @param  data    Source buffer. Must not be NULL when length > 0.
  * @param  length  Number of bytes to write. A zero length is a no-op.
  * @note   F-RAM writes complete at bus speed; no delay or ACK polling is
  *         required after a successful transfer.
  */
FramStatus FRAM_Write(uint16_t address, const uint8_t *data, uint16_t length);

/**
  * @brief  Probe both address blocks and preserve the failure reason.
  */
FramStatus FRAM_ProbeStatus(void);

/**
  * @brief  Probe whether both address blocks acknowledge.
  * @retval true when the FM24CL04B is reachable, otherwise false.
  */
bool FRAM_Probe(void);

#ifdef __cplusplus
}
#endif

#endif /* FRAM_DRIVER_H_ */