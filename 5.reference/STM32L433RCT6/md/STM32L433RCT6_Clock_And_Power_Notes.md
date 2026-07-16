<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Clock and Power Notes

## Clock sources

The STM32L433xx clock system supports:

| Clock | Description | Project use |
|---|---|---|
| MSI | Internal multispeed 100 kHz to 48 MHz oscillator | Default after reset; good for low-power operation |
| HSI16 | Internal 16 MHz RC | Stable internal source; can feed PLL |
| HSI48 | Internal 48 MHz RC with CRS | USB/RNG/SDMMC support |
| HSE | External 4-48 MHz oscillator/crystal | Optional high accuracy high-speed source |
| LSE | External 32.768 kHz crystal | Recommended for RTC/water meter timestamp |
| LSI | Internal low-power 32 kHz RC | IWDG, backup low-accuracy timing |
| PLL / PLLSAI1 | Clock multiplication and peripheral clocks | 80 MHz runtime, USB/ADC/SAI clocks |

The device starts after reset with MSI around 4 MHz. Application firmware can switch later to HSI16 or PLL up to 80 MHz.

## Recommended clock policy

### Bring-up mode

```text
SYSCLK = MSI or HSI16
SPI1   = low speed, e.g. 1 MHz effective SCK
UART   = 115200 baud
RTC    = optional initially
```

This reduces clock tree complexity while validating SWD, UART, GPIO and SPI.

### Normal measurement processing

```text
SYSCLK = 40-80 MHz only during processing windows
SPI1   = 4-8 MHz for MAX35103/MAX35101
RTC    = LSE
```

The MAX35103/MAX35101 SPI limit is below what STM32 SPI can generate, so use a conservative SCK such as 4-8 MHz after bring-up.

### Low-power idle

```text
MCU    = Stop 2
RTC    = LSE active
LPTIM1 = optional active
Wake   = MAX35103 INT or RTC/LPTIM event
```

MAX35103 event timing can run while MCU sleeps. The MCU wakes, reads data, processes it and returns to Stop 2.

## Power domains

| Domain | Notes |
|---|---|
| VDD | Main digital/I/O supply, 1.71 V to 3.6 V |
| VDDA | Analog supply for ADC/DAC/OPAMP/COMP/VREFBUF |
| VDDUSB | USB transceiver supply, 3.0 V to 3.6 V when USB used |
| VBAT | Backup domain for RTC/LSE/backup registers |
| VDD12 | External SMPS option only; not assumed for `STM32L433RCT6` |

For the water meter prototype, use a 3.3 V rail for VDD and MAX35103/MAX35101. Tie VDDA appropriately to VDD through analog filtering if ADC diagnostics are used.

## Voltage scaling

The MCU supports dynamic voltage scaling:

| Range | Max frequency | Use |
|---|---:|---|
| Range 1 | 80 MHz | Processing/calibration/communication bursts |
| Range 2 | 26 MHz | Lower-power runtime where full speed is not required |
| Low-power run | up to 2 MHz | Slow background tasks |

Recommendation:

- Start simple: use Range 1 for firmware bring-up.
- Optimize later: switch to lower frequency/range for long idle/slow tasks.
- The biggest power savings will come from Stop 2 duty cycling, not from micro-optimizing run current first.

## Low-power modes relevant to this project

| Mode | SRAM retained | Typical use |
|---|---|---|
| Sleep | Yes | Short CPU idle while peripherals active |
| Low-power run/sleep | Yes | Slow continuous work |
| Stop 0 | Yes | Fast wake, higher current |
| Stop 1 | Yes | More wake sources, more current than Stop 2 |
| Stop 2 | Yes | Main water meter idle mode |
| Standby | SRAM1 lost, optional SRAM2 retention | Long idle/shipping mode |
| Shutdown | SRAM lost | Deepest mode, limited wake and reset-like resume |

Stop 2 is the best default for periodic metrology because it retains SRAM and gives very low current.

## Wake-up sources to use

| Wake source | Use |
|---|---|
| PC13 / EXTI13 | MAX35103/MAX35101 `INT` |
| RTC wakeup timer | Periodic housekeeping |
| LPTIM1 | Low-power timing and timeout |
| LPUART1 | Optional communication wake from Stop 2 |
| IWDG | Fault recovery |

## Clock and power sequence for measurement

```text
1. MCU sleeps in Stop 2.
2. MAX35103 asserts INT low after measurement/event timing.
3. PC13 EXTI wakes MCU.
4. MCU restores/ensures system clock.
5. Enable SPI1 and GPIO clocks if disabled.
6. Assert CE and read MAX interrupt/status/result registers.
7. Run filtering/calibration/flow computation.
8. Store/update small state.
9. Optionally transmit/log.
10. Disable unused clocks/peripherals.
11. Return to Stop 2.
```

## Practical SPI clock plan

| Phase | SPI SCK target | Reason |
|---|---:|---|
| Board bring-up | 500 kHz - 1 MHz | Safe with long wires, logic analyzer, early bugs |
| Prototype stable | 2-4 MHz | Reliable and fast enough |
| Product | 4-8 MHz | Conservative relative to both MCU and MAX limits |
| Upper limit | Check MAX datasheet | STM32 SPI can exceed MAX requirement, so MAX is the limiting side |

## Power optimization checklist

- Put unused GPIOs into analog mode.
- Disable peripheral clocks when not used.
- Avoid leaving UART/RS485/LoRa modules powered during idle.
- Use interrupt-driven SPI readout; avoid polling loops.
- Keep computation short and bounded after each wakeup.
- Use CRC/checkpoints to recover from reset without long recalibration when safe.
- Measure current at board level, not just estimate from datasheet.

## Clock and power conclusion

The recommended first product strategy is **Stop 2 + LSE RTC + MAX INT wakeup + short 80 MHz processing burst**. This makes the MCU powerful enough for calibration algorithms while keeping average power low.
