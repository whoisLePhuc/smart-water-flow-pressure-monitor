<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Technical Summary

## Purpose in the water meter system

`STM32L433RCT6` should be treated as the **metrology controller**:

```text
MAX35103/MAX35101 = ultrasonic AFE + TDC + TOF timing engine
STM32L433RCT6     = configuration + calibration + filtering + flow calculation + communication + power management
```

The MCU does not need to measure picosecond timing directly. It controls the MAX35103/MAX35101 through SPI, reads interrupt/status/results, runs compensation algorithms, stores calibration data and manages the rest of the system.

## Core and compute

| Feature | Value |
|---|---|
| CPU | Arm Cortex-M4 |
| FPU | Single-precision FPU |
| DSP instructions | Yes |
| MPU | Yes |
| Max frequency | 80 MHz |
| Performance | 100 DMIPS class, 1.25 DMIPS/MHz |
| ART accelerator | Yes, improves flash execution performance |

Implication for this project:

- Floating-point flow equations are acceptable.
- DSP/filtering operations such as moving average, IIR, median window and sigma clipping are acceptable.
- Avoid large matrix-heavy algorithms or large signal-processing buffers because SRAM is limited.

## Memory

| Memory | Size / structure | Project implication |
|---|---|---|
| Flash | 256 KB single-bank | Enough for compact firmware, not ideal for large OTA or heavy middleware |
| SRAM1 | 48 KB at `0x20000000` | Main runtime data, stack, heap, buffers |
| SRAM2 | 16 KB at `0x10000000`, also aliased at `0x2000C000` | Useful for retained state / critical data; can be retained in Standby |
| Backup registers | 32 x 32-bit = 128 bytes | Useful for reset reason, boot flags, small metrology state |

Recommended policy:

- Keep firmware modular but small.
- Avoid dynamic allocation in core metrology modules.
- Store large logs or calibration history in external EEPROM/FRAM/Flash.
- Use SRAM2 for small retained state, not for large measurement history.

## Supply and operating conditions

| Item | Value |
|---|---|
| VDD | 1.71 V to 3.6 V |
| VDDA | Depends on analog use; tie to VDD if analog independent supply is not separated |
| VDDUSB | 3.0 V to 3.6 V when USB is used; may be tied/handled according to datasheet if unused |
| VBAT | 1.55 V to 3.6 V for RTC/backup domain |
| Temperature suffix `6` | -40 to +85 degC ambient, +105 degC junction |

For a MAX35103/MAX35101 design, a **3.3 V system rail** is the simplest choice because it is compatible with both the MCU and MAX35103/MAX35101 SPI interface.

## Clocks

| Clock source | Notes |
|---|---|
| MSI | Startup clock; 100 kHz to 48 MHz; can be LSE-trimmed |
| HSI16 | Internal 16 MHz RC |
| HSI48 | USB/RNG/SDMMC clock source with CRS |
| HSE | 4 MHz to 48 MHz external crystal/resonator |
| LSE | 32.768 kHz external crystal for RTC |
| LSI | Internal low-power 32 kHz RC, typical for IWDG |
| PLL | System clock up to 80 MHz |

Recommended project clock policy:

- Bring-up: MSI or HSI16, no PLL at first.
- Normal processing: use PLL to 80 MHz only when needed.
- Metering idle: Stop 2 with LSE running for RTC.
- MAX35103 readout: wake by `INT`, enable SPI, read results, process quickly, sleep again.

## Peripherals useful for this project

| Peripheral | Use |
|---|---|
| SPI1 | MAX35103/MAX35101 command/register/result access |
| GPIO/EXTI | MAX35103 `INT`, `CE`, `RST`, optional `WDO` |
| USART2 or LPUART1 | Debug, RS485, external communication module |
| I2C | EEPROM/FRAM, sensors, external RTC if needed |
| RTC | Timestamp, periodic wake, low-power scheduling |
| LPTIM1 | Low-power time base in Stop mode |
| DMA | Optional for SPI/UART; not required for simple MAX35103 transactions |
| CRC | Calibration/config integrity |
| IWDG | System watchdog for production firmware |
| ADC | Battery voltage, board temperature, analog diagnostics |
| RNG | Optional security/randomization |
| USB FS | Useful for service/debug, but increases hardware and power complexity |

## Suitability rating

| Area | Rating | Comment |
|---|---|---|
| MAX35103 SPI control | Excellent | SPI is much faster than required |
| Low-power metering | Good | Stop 2 and RTC are suitable |
| Calibration algorithms | Good | M4F is sufficient; memory is the main limit |
| Large logging | Limited | Use external storage |
| OTA | Limited | 256 KB flash makes dual-bank OTA difficult |
| PCB practicality | Good | LQFP64 is easy to route and inspect |

## Final conclusion

`STM32L433RCT6` is a good practical MCU for a first product/prototype if the firmware stays focused and storage is externalized. If future requirements include heavy protocol stacks, large calibration databases, file systems or robust OTA, consider a larger STM32L4/U5 device.
