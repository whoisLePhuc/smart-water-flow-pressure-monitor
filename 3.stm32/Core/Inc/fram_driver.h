/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : fram_driver.h
  * @brief          : FM24CL04B FRAM driver over I2C1
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __FRAM_DRIVER_H
#define __FRAM_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* FM24CL04B constants -------------------------------------------------------*/
#define FRAM_SIZE_BYTES     512u
#define FRAM_PAGE_SIZE      256u

/* I2C slave addresses (7-bit). A0 = GND → base = 0x50.
   Bit 8 of the logical address selects the page:
     address 0-255   → slave 0x50
     address 256-511 → slave 0x51  */
#define FRAM_SLAVE_ADDR_P0 0x50u
#define FRAM_SLAVE_ADDR_P1 0x51u

#define FRAM_TIMEOUT_MS    100u

/* Status codes --------------------------------------------------------------*/
typedef enum {
    FRAM_OK = 0,
    FRAM_OUT_OF_RANGE,
    FRAM_IO_ERROR,
    FRAM_INVALID_PARAM,
    FRAM_WRITE_PROTECTED,
    FRAM_TIMEOUT
} FramStatus;

/* Public functions ----------------------------------------------------------*/

/**
  * @brief  Initialise the FRAM driver (currently a no-op, I2C is already
  *         initialised by CubeMX).
  * @retval FRAM_OK
  */
FramStatus FRAM_Init(void);

/**
  * @brief  Read a block of data from FRAM.
  * @param  address  Logical byte address (0 .. FRAM_SIZE_BYTES-1)
  * @param  buffer   Caller-owned destination buffer
  * @param  length   Number of bytes to read
  * @retval FramStatus
  */
FramStatus FRAM_Read(uint16_t address, uint8_t *buffer, uint16_t length);

/**
  * @brief  Write a block of data to FRAM.
  * @param  address  Logical byte address (0 .. FRAM_SIZE_BYTES-1)
  * @param  data     Caller-owned source buffer
  * @param  length   Number of bytes to write
  * @retval FramStatus
  */
FramStatus FRAM_Write(uint16_t address, const uint8_t *data, uint16_t length);

/**
  * @brief  Probe whether the FRAM device is present on the bus.
  * @retval true if both pages acknowledge, false otherwise
  */
bool FRAM_Probe(void);

#ifdef __cplusplus
}
#endif

#endif /* __FRAM_DRIVER_H */
