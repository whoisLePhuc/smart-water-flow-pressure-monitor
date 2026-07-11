<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Technical Documentation

This folder contains PDF-checked planning notes for using **STM32L433RCT6** as the main MCU in a smart ultrasonic water meter project with **MAX35103/MAX35101**.

## Device target

| Item | Value |
|---|---|
| Ordering code | `STM32L433RCT6` |
| Family | STM32L433xx |
| Core | Arm Cortex-M4 with single-precision FPU |
| Max CPU frequency | 80 MHz |
| Flash | 256 KB, single-bank |
| SRAM | 64 KB total: 48 KB SRAM1 + 16 KB SRAM2 |
| Package | LQFP64, 10 mm x 10 mm |
| Temperature suffix | `6`: -40 to +85 degC ambient, +105 degC junction |
| Main role | Low-power metrology controller for MAX35103/MAX35101 |

## Recommended reading order

1. `STM32L433RCT6_Technical_Summary.md`
2. `STM32L433RCT6_Resource_Map.md`
3. `STM32L433RCT6_Pinout_And_PCB_Notes.md`
4. `STM32L433RCT6_Clock_And_Power_Notes.md`
5. `STM32L433RCT6_MAX35103_Integration_Notes.md`
6. `STM32L433RCT6_Low_Power_Strategy.md`
7. `STM32L433RCT6_Memory_And_Flash_Notes.md`
8. `STM32L433RCT6_Peripheral_Notes.md`
9. `STM32L433RCT6_Firmware_Architecture_Notes.md`
10. `STM32L433RCT6_Development_And_Debug_Notes.md`
11. `STM32L433RCT6_Errata_Check_Report.md`
12. `STM32L433RCT6_Board_Config_Review.md`
13. `STM32L433RCT6_PDF_Check_Report.md`

## Project-level conclusion

`STM32L433RCT6` is suitable for a compact low-power ultrasonic water meter when the metrology timing is handled by MAX35103/MAX35101.

It is a good fit for:

- SPI driver for MAX35103/MAX35101.
- Interrupt-driven measurement readout.
- Zero-flow calibration.
- Temperature compensation.
- Filtering and outlier rejection.
- Small or medium flow correction tables.
- UART/RS485/LoRa module control.
- Stop 2 based low-power operation.

It needs careful resource management for:

- Firmware size, because flash is 256 KB.
- Runtime buffers, because SRAM is 64 KB.
- Long-term logging, which should use external EEPROM/FRAM/flash if needed.
- OTA, which is difficult with only 256 KB internal flash unless the design is very constrained or uses external memory.

## Source documents still required for final firmware/PCB

The uploaded PDF is the datasheet. For register-level firmware and production PCB review, also use:

| Document | Purpose |
|---|---|
| `RM0394` | Reference manual for registers, RCC, PWR, GPIO, SPI, DMA, RTC, EXTI, Flash, etc. |
| `ES0318` | Device errata for STM32L433xx. |
| `PM0214` | Cortex-M4 programming manual. |
| `AN2606` | STM32 bootloader/system memory boot mode. |
| ST GPIO low-power application notes | GPIO state, unused pins, low leakage design. |
| MAX35103/MAX35101 datasheet | SPI opcode, interrupt, timing and metrology behavior. |

## Proposed repo location

```text
docs/
└── stm32l433rct6/
    ├── README.md
    ├── STM32L433RCT6_Technical_Summary.md
    ├── STM32L433RCT6_Resource_Map.md
    ├── STM32L433RCT6_Pinout_And_PCB_Notes.md
    ├── STM32L433RCT6_Clock_And_Power_Notes.md
    ├── STM32L433RCT6_Peripheral_Notes.md
    ├── STM32L433RCT6_Memory_And_Flash_Notes.md
    ├── STM32L433RCT6_Firmware_Architecture_Notes.md
    ├── STM32L433RCT6_MAX35103_Integration_Notes.md
    ├── STM32L433RCT6_Low_Power_Strategy.md
    ├── STM32L433RCT6_Development_And_Debug_Notes.md
    ├── STM32L433RCT6_Errata_Check_Report.md
    ├── STM32L433RCT6_Board_Config_Review.md
    └── STM32L433RCT6_PDF_Check_Report.md
```
