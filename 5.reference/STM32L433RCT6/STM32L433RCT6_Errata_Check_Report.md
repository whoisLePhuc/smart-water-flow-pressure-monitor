<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Errata Check Report

## Status

The uploaded file is the **datasheet DS11449 Rev 8**, not the errata sheet.

The datasheet introduction says this device should be checked against the STM32L433xx errata sheet **ES0318**.

Therefore this file is a placeholder/checklist until ES0318 is added to the reference folder.

## Errata document required

| Needed document | Purpose |
|---|---|
| ES0318 | Device limitations and workarounds for STM32L433xx |
| RM0394 | Register-level behavior and details |
| DS11449 | Electrical/mechanical/feature reference |

## Blocks that must be checked against errata

| Block | Why it matters for water meter |
|---|---|
| RCC/clock | Stop/wakeup clock restoration, MSI/LSE behavior |
| PWR/low-power modes | Stop 2, Standby, wakeup behavior |
| GPIO/EXTI | MAX35103 `INT` wakeup reliability |
| SPI | MAX35103/MAX35101 communication |
| RTC/LSE | Measurement scheduling and timestamps |
| I2C | External EEPROM/FRAM/sensors |
| USART/LPUART | Debug, RS485/modem, low-power wake |
| Flash | Calibration storage, option bytes, ECC |
| ADC/VREFINT | Battery and diagnostics |
| IWDG | Fault recovery in low-power states |
| DMA | SPI/UART/I2C transfer if DMA is used |

## Suggested errata review table

| Errata item | Affected block | Impact to project | Workaround | Test required |
|---|---|---|---|---|
| TBD after ES0318 review | TBD | TBD | TBD | TBD |

## Temporary conservative design rules

Until ES0318 is reviewed:

- Keep SPI transactions simple and timeout-protected.
- Avoid relying on DMA for the first MAX driver version.
- Validate Stop 2 wake from PC13 on real hardware.
- Validate RTC wake and LSE startup on real hardware.
- Keep a hardware reset path to MAX35103/MAX35101.
- Use watchdog breadcrumbs to detect low-power wake/recovery issues.
- Avoid irreversible option byte changes during early development.

## Conclusion

Do not freeze product firmware or PCB assumptions until ES0318 has been reviewed. This datasheet confirms that STM32L433RCT6 is appropriate, but errata review is still required for production reliability.
