<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Board Config Review

This note reviews the proposed `board_config.h` for the corrected target MCU: **STM32L433RCT6**.

## Original target correction

The previous board config was written for:

```text
STM32L476RG LQFP64
```

The correct target is:

```text
STM32L433RCT6 LQFP64
```

The main interface mapping still works, but memory assumptions and some documentation labels must change.

## MAX35103/MAX35101 mapping

The following mapping is valid for STM32L433RCT6 LQFP64:

```c
#define MAX35103_SPI                     SPI1
#define MAX35103_SPI_SCK_PORT            GPIOA
#define MAX35103_SPI_SCK_PIN             GPIO_PIN_5
#define MAX35103_SPI_SCK_AF              GPIO_AF5_SPI1

#define MAX35103_SPI_MISO_PORT           GPIOA
#define MAX35103_SPI_MISO_PIN            GPIO_PIN_6
#define MAX35103_SPI_MISO_AF             GPIO_AF5_SPI1

#define MAX35103_SPI_MOSI_PORT           GPIOA
#define MAX35103_SPI_MOSI_PIN            GPIO_PIN_7
#define MAX35103_SPI_MOSI_AF             GPIO_AF5_SPI1

#define MAX35103_CE_PORT                 GPIOA
#define MAX35103_CE_PIN                  GPIO_PIN_4

#define MAX35103_RST_PORT                GPIOB
#define MAX35103_RST_PIN                 GPIO_PIN_0

#define MAX35103_INT_PORT                GPIOC
#define MAX35103_INT_PIN                 GPIO_PIN_13
#define MAX35103_INT_EXTI_IRQn           EXTI15_10_IRQn

#define MAX35103_WDO_PORT                GPIOB
#define MAX35103_WDO_PIN                 GPIO_PIN_1
```

## Required UART fix

Do **not** use PD5/PD6 for USART2 on STM32L433RCT6 LQFP64.

Use PA2/PA3:

```c
#define DEBUG_UART                       USART2
#define DEBUG_UART_CLK_ENABLE()          __HAL_RCC_USART2_CLK_ENABLE()

#define DEBUG_UART_TX_PORT               GPIOA
#define DEBUG_UART_TX_PIN                GPIO_PIN_2
#define DEBUG_UART_TX_AF                 GPIO_AF7_USART2

#define DEBUG_UART_RX_PORT               GPIOA
#define DEBUG_UART_RX_PIN                GPIO_PIN_3
#define DEBUG_UART_RX_AF                 GPIO_AF7_USART2

#define DEBUG_UART_BAUDRATE              115200u
```

## MCU identity defines

Add:

```c
#define BOARD_MCU_FAMILY                 "STM32L433xx"
#define BOARD_MCU_PART                   "STM32L433RCT6"
#define BOARD_MCU_PACKAGE                "LQFP64"
#define BOARD_MCU_FLASH_BYTES            (256u * 1024u)
#define BOARD_MCU_SRAM_BYTES             (64u * 1024u)
```

## GPIO clock enable helper

Add:

```c
#define MAX35103_GPIO_CLK_ENABLE()       \
    do {                                 \
        __HAL_RCC_GPIOA_CLK_ENABLE();    \
        __HAL_RCC_GPIOB_CLK_ENABLE();    \
        __HAL_RCC_GPIOC_CLK_ENABLE();    \
    } while (0)

#define DEBUG_UART_GPIO_CLK_ENABLE()     \
    do {                                 \
        __HAL_RCC_GPIOA_CLK_ENABLE();    \
    } while (0)
```

## SPI mode defines

Add explicit SPI configuration constants:

```c
#define MAX35103_SPI_MODE                SPI_MODE_MASTER
#define MAX35103_SPI_DIRECTION           SPI_DIRECTION_2LINES
#define MAX35103_SPI_DATASIZE            SPI_DATASIZE_8BIT
#define MAX35103_SPI_NSS                 SPI_NSS_SOFT
#define MAX35103_SPI_FIRSTBIT            SPI_FIRSTBIT_MSB
```

Clock polarity and phase must be set according to MAX35103/MAX35101 datasheet and confirmed with a logic analyzer.

## Baudrate notes

The proposed SPI speeds are reasonable:

```c
#define MAX35103_SPI_BAUDRATE_INIT       1000000u
#define MAX35103_SPI_BAUDRATE_PRODUCTION 8000000u
```

STM32L433 SPI can run faster than this, but the MAX IC and PCB signal integrity set the practical production limit.

## Low-power note

The comment:

```c
// Default sleep mode: Stop 2 (SRAM retained, ~1 µA, wake by EXTI)
```

is directionally correct. Datasheet typical Stop 2 current depends strongly on VDD, temperature, RTC/LCD state and board design. Do not treat `~1 µA` as a board-level guarantee.

## Corrected board_config skeleton

```c
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_MCU_FAMILY                 "STM32L433xx"
#define BOARD_MCU_PART                   "STM32L433RCT6"
#define BOARD_MCU_PACKAGE                "LQFP64"
#define BOARD_MCU_FLASH_BYTES            (256u * 1024u)
#define BOARD_MCU_SRAM_BYTES             (64u * 1024u)

#define MAX35103_SPI                     SPI1
#define MAX35103_SPI_CLK_ENABLE()        __HAL_RCC_SPI1_CLK_ENABLE()
#define MAX35103_SPI_CLK_DISABLE()       __HAL_RCC_SPI1_CLK_DISABLE()

#define MAX35103_GPIO_CLK_ENABLE()       \
    do {                                 \
        __HAL_RCC_GPIOA_CLK_ENABLE();    \
        __HAL_RCC_GPIOB_CLK_ENABLE();    \
        __HAL_RCC_GPIOC_CLK_ENABLE();    \
    } while (0)

#define MAX35103_SPI_SCK_PORT            GPIOA
#define MAX35103_SPI_SCK_PIN             GPIO_PIN_5
#define MAX35103_SPI_SCK_AF              GPIO_AF5_SPI1

#define MAX35103_SPI_MISO_PORT           GPIOA
#define MAX35103_SPI_MISO_PIN            GPIO_PIN_6
#define MAX35103_SPI_MISO_AF             GPIO_AF5_SPI1

#define MAX35103_SPI_MOSI_PORT           GPIOA
#define MAX35103_SPI_MOSI_PIN            GPIO_PIN_7
#define MAX35103_SPI_MOSI_AF             GPIO_AF5_SPI1

#define MAX35103_CE_PORT                 GPIOA
#define MAX35103_CE_PIN                  GPIO_PIN_4
#define MAX35103_RST_PORT                GPIOB
#define MAX35103_RST_PIN                 GPIO_PIN_0
#define MAX35103_INT_PORT                GPIOC
#define MAX35103_INT_PIN                 GPIO_PIN_13
#define MAX35103_INT_EXTI_IRQn           EXTI15_10_IRQn
#define MAX35103_WDO_PORT                GPIOB
#define MAX35103_WDO_PIN                 GPIO_PIN_1

#define MAX35103_SPI_BAUDRATE_INIT       1000000u
#define MAX35103_SPI_BAUDRATE_PRODUCTION 8000000u
#define MAX35103_SPI_TIMEOUT_MS          100u

#define DEBUG_UART                       USART2
#define DEBUG_UART_CLK_ENABLE()          __HAL_RCC_USART2_CLK_ENABLE()
#define DEBUG_UART_GPIO_CLK_ENABLE()     \
    do {                                 \
        __HAL_RCC_GPIOA_CLK_ENABLE();    \
    } while (0)
#define DEBUG_UART_TX_PORT               GPIOA
#define DEBUG_UART_TX_PIN                GPIO_PIN_2
#define DEBUG_UART_TX_AF                 GPIO_AF7_USART2
#define DEBUG_UART_RX_PORT               GPIOA
#define DEBUG_UART_RX_PIN                GPIO_PIN_3
#define DEBUG_UART_RX_AF                 GPIO_AF7_USART2
#define DEBUG_UART_BAUDRATE              115200u

#define SYSTEM_CLOCK_HZ                  80000000u
#define RTC_CLOCK_SOURCE                 RCC_RTCCLKSOURCE_LSE
#define LP_MODE_STOP2

#ifdef __cplusplus
}
#endif

#endif /* BOARD_CONFIG_H */
```

## Review conclusion

Your MAX35103 mapping is still good for STM32L433RCT6. The important updates are:

1. Change target identity from STM32L476RG to STM32L433RCT6.
2. Update Flash/SRAM assumptions to 256 KB / 64 KB.
3. Use USART2 on PA2/PA3, not PD5/PD6.
4. Keep PC13 as MAX `INT` wake input.
5. Add GPIO clock enable and explicit SPI settings.
