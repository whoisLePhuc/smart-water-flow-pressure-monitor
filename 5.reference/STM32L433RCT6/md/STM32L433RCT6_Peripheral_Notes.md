<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Peripheral Notes

## SPI

The datasheet states that the STM32L433xx has three SPI interfaces and can communicate up to 40 Mbit/s in master/slave modes. All SPI interfaces can be served by DMA.

### Project use

| SPI instance | Proposed use |
|---|---|
| SPI1 | MAX35103/MAX35101 |
| SPI2 | Spare, optional external device |
| SPI3 | Spare, optional debug/secondary device |

### SPI1 for MAX35103/MAX35101

| Parameter | Recommended value |
|---|---|
| Mode | Master |
| Direction | Full-duplex 2-line |
| Data size | 8-bit |
| First bit | MSB first |
| NSS | Software GPIO CE |
| Initial speed | 1 MHz |
| Production speed | 4-8 MHz |
| DMA | Optional, not required for short opcode/register transactions |

## GPIO and EXTI

GPIO pins can be configured as input, output, analog or alternate function. EXTI has edge-detect lines and can wake the system from Stop mode.

### Project use

| Signal | GPIO mode |
|---|---|
| MAX CE | Output push-pull, default high |
| MAX RST | Output push-pull, default high after reset sequence |
| MAX INT | Input with pull-up, EXTI falling edge |
| MAX WDO | Input with pull-up, optional EXTI if used |
| Unused pins | Analog mode, no pull unless required |

## USART and LPUART

The MCU has USART1, USART2, USART3 and LPUART1. USARTs support DMA, RS485 driver enable and wake from Stop 0/1. LPUART1 can wake from Stop 2.

### Project recommendations

| Interface | Use |
|---|---|
| USART2 PA2/PA3 | Debug console and early firmware logs |
| USART1 or USART3 | RS485 or external modem if needed |
| LPUART1 | Low-power communication wakeup if the product requires it |

If ultra-low-power serial wakeup is needed, prefer LPUART1 over standard USART.

## I2C

The MCU has three I2C interfaces supporting standard, fast and fast-mode plus operation. I2C3 can wake from Stop 2 on address match.

### Project use

| I2C use | Recommendation |
|---|---|
| External EEPROM/FRAM | Use I2C1 or I2C3 |
| Low-power external sensor | Consider I2C3 if Stop 2 wake is required |
| Pull-ups | Size for bus capacitance and power budget |

## RTC and backup registers

The RTC supports calendar, alarms, wakeup timer and calibration. Backup registers provide 128 bytes of state retained by VBAT.

### Project use

- Timestamp measurements.
- Schedule periodic self-test/calibration windows.
- Store last reset reason and boot counters.
- Store last valid metrology summary in backup registers if useful.

## LPTIM1 and LPTIM2

LPTIM1 can operate in Stop 0/1/2 if clocked properly. LPTIM2 operates in Stop 0/1.

### Project use

- Use LPTIM1 for low-power timeout handling.
- Use RTC for calendar-level scheduling.
- Use LPTIM for short low-power intervals if needed.

## DMA

The MCU has DMA1 and DMA2 with 14 channels total.

### Project use

DMA is optional. For MAX35103/MAX35101 transactions, blocking SPI with a timeout is often simpler and acceptable because transactions are short.

Use DMA when:

- UART communication becomes heavy.
- External logging transfers become large.
- SPI transactions grow because of external memory or bulk data.

## ADC

ADC1 is useful for diagnostics:

- Battery voltage via divider.
- Internal temperature sensor.
- VREFINT for supply estimation.
- Analog diagnostics if the board exposes them.

Do not use the internal temperature sensor as the main water temperature sensor. For metrology, use MAX35103/MAX35101 RTD support or an external calibrated water temperature sensor.

## CRC

Use the CRC peripheral to protect:

- Calibration records.
- Configuration blocks.
- Firmware metadata.
- Logged measurement records.

## IWDG and WWDG

Use IWDG in product firmware because it is clocked independently from LSI and can operate in low-power scenarios. WWDG is useful for tighter runtime supervision but is less important in the first prototype.

## USB FS

USB is useful for service mode but increases hardware and firmware complexity. It requires correct VDDUSB handling and a precise 48 MHz clock.

Recommendation:

- Leave USB out of the first metrology PCB unless service interface is a hard requirement.
- Use UART/SWD for bring-up and factory debug first.

## CAN

CAN is available and can be useful for industrial flow-meter variants, but it requires a transceiver and protection. Keep it optional unless the product communication requirement includes CAN.

## LCD

The LCD controller can be useful if the water meter needs a segment LCD display. It can operate in Stop mode, but pin allocation becomes more constrained.

## QUADSPI

QUADSPI can connect external flash. Use it if:

- Large logs are required.
- A large communication stack or asset storage is needed.
- OTA with external staging is required.

For a simple meter, external I2C FRAM/EEPROM may be easier than QUADSPI flash.

## Peripheral conclusion

For the first hardware revision, keep the active peripheral set small:

```text
SPI1 + GPIO/EXTI + RTC + LPTIM1 + USART2 + I2C + ADC + CRC + IWDG
```

Add USB/CAN/LCD/QUADSPI only when the product requirement justifies the extra pins, firmware and power cost.
