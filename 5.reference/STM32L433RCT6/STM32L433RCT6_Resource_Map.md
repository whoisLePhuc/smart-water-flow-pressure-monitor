<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Resource Map

This document maps MCU resources to the ultrasonic water meter system.

## High-level allocation

| Resource | Availability on STM32L433RCT6 | Proposed use | Priority |
|---|---|---|---|
| SPI1 | Yes | MAX35103/MAX35101 | Required |
| GPIO EXTI | Yes | MAX35103 `INT` wake/readout | Required |
| GPIO output | Yes | MAX35103 `CE`, `RST` | Required |
| GPIO input | Yes | MAX35103 `WDO` optional | Recommended |
| USART2 | Yes | Debug UART or RS485 | Recommended |
| LPUART1 | Yes | Low-power communication wakeup | Optional/recommended |
| I2C1/I2C3 | Yes | External EEPROM/FRAM/sensors | Recommended |
| RTC | Yes | Timestamp and periodic metering | Required for low-power product |
| LPTIM1 | Yes | Low-power timer in Stop 2 | Recommended |
| DMA1/DMA2 | 14 channels total | Optional SPI/UART offload | Optional |
| CRC | Yes | Config/calibration integrity | Recommended |
| IWDG | Yes | Watchdog | Required for product |
| ADC1 | Yes | Battery/VDDA/VBAT/diagnostics | Recommended |
| USB FS | Yes | Service interface | Optional |
| CAN | Yes | Industrial communication option | Optional |
| LCD | Yes | Segment LCD if product has local display | Optional |
| QUADSPI | Yes | External memory | Optional |

## Proposed pin allocation for MAX35103/MAX35101

| STM32 pin | MCU function | MAX35103/MAX35101 pin | Notes |
|---|---|---|---|
| PA5 | SPI1_SCK | SCK | AF5, hardware SPI clock |
| PA6 | SPI1_MISO | DOUT | AF5, MAX to MCU |
| PA7 | SPI1_MOSI | DIN | AF5, MCU to MAX |
| PA4 | GPIO output | CE | Software chip-enable, active-low |
| PB0 | GPIO output | RST | Active-low reset |
| PC13 | EXTI13 input | INT | Active-low open-drain interrupt; pull-up required |
| PB1 | GPIO input | WDO | Optional watchdog output; pull-up required if used |

This mapping is valid on STM32L433Rx LQFP64 according to the pinout and pin definition tables.

## Debug/communication allocation

Preferred debug UART:

| STM32 pin | Function | Notes |
|---|---|---|
| PA2 | USART2_TX | Available on LQFP64; also wakeup pin `WKUP4/LSCO` |
| PA3 | USART2_RX | Available on LQFP64 |

Avoid `PD5/PD6` for USART2 on this package because LQFP64 does not expose those pins.

Possible external communication options:

| Interface | Candidate pins | Use case |
|---|---|---|
| USART2 | PA2/PA3 | Debug, RS485, modem |
| USART1 | PA9/PA10 or PB6/PB7 | Secondary UART, module control |
| LPUART1 | PC0/PC1 or PB10/PB11 or PA2/PA3 | Low-power command wakeup |
| I2C1 | PB6/PB7 or PB8/PB9 | EEPROM/FRAM/sensor bus |
| I2C3 | PC0/PC1 or PA7/PB4 alternatives | Low-power sensor bus; I2C3 supports Stop 2 address-match wakeup |

## Memory resource budget

| Resource | Size | Suggested budget |
|---|---:|---|
| Flash total | 256 KB | Boot + app + config defaults |
| SRAM total | 64 KB | Stack, drivers, metrology buffers, communication buffers |
| SRAM1 | 48 KB | Main runtime memory |
| SRAM2 | 16 KB | Retained state / critical diagnostic state |
| Backup registers | 128 bytes | Boot flags, reset reason, last valid measurement summary |

Suggested first firmware budget:

| Module | Flash target | RAM target |
|---|---:|---:|
| BSP/HAL/LL startup | 25-50 KB | 2-5 KB |
| MAX35103 driver | 8-15 KB | < 1 KB |
| Metrology/calibration/filtering | 20-45 KB | 2-8 KB |
| Storage/config | 8-20 KB | 1-4 KB |
| Communication | 15-60 KB | 2-12 KB |
| Diagnostics/logging | 10-30 KB | 1-8 KB |
| FreeRTOS, if used | 10-30 KB | depends on task stacks |

## Peripheral conflict notes

| Conflict | Recommendation |
|---|---|
| PA2/PA3 can be USART2 or LPUART1 | Pick one function early. For simple debug, use USART2. For low-power wake communication, consider LPUART1. |
| PA4/PA5 are also analog/DAC-capable | If used for MAX SPI/CE, do not allocate them to analog functions. |
| PC13 is a low-power/wakeup pin | Good for MAX `INT`, but keep it as input only; avoid high-speed output use. |
| PA13/PA14 are SWD | Reserve for debug; do not repurpose in early hardware. |
| PB3/PB4 are JTAG/SWO related | Can be reused after disabling JTAG, but avoid unless needed. |
| PC14/PC15 are LSE pins | Reserve for 32.768 kHz crystal if accurate RTC is required. |

## Minimal product resource allocation

```text
SPI1      -> MAX35103/MAX35101
EXTI13    -> MAX35103 INT
GPIO      -> CE/RST/WDO
LSE + RTC -> timestamp and low-power scheduling
LPTIM1    -> low-power timer support
I2C1      -> external EEPROM/FRAM
USART2    -> debug/RS485 during prototype
ADC1      -> battery and diagnostic measurement
IWDG      -> watchdog
CRC       -> config/calibration checksum
SWD       -> programming/debug
```

## Resource conclusion

The selected MCU has enough peripheral resources for a MAX35103-based meter. The limiting resources are not SPI or CPU speed; they are **Flash, SRAM, and pin allocation discipline**.
