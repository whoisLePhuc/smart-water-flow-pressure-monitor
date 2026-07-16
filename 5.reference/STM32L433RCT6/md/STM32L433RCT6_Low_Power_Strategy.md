<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Low-Power Strategy

## System power goal

The water meter should spend most time asleep. The MAX35103/MAX35101 can perform scheduled/event timing measurements and wake the MCU through `INT`.

Target behavior:

```text
MCU sleeps -> MAX measures -> MAX INT -> MCU wakes -> read/process/store -> MCU sleeps
```

## Recommended low-power state machine

| State | STM32 | MAX35103/MAX35101 | Communication | Purpose |
|---|---|---|---|---|
| Boot | Run | Reset/init | Off | Hardware and driver init |
| Configure | Run | Register config/calibration | Off | Prepare measurement |
| Meter idle | Stop 2 | Event timing active | Off | Main low-power state |
| Readout | Run | Result available | Off | Read TOF/temp/status |
| Process | Run | Idle or next event armed | Off | Filter/calibrate/calculate |
| Store | Run | Idle/event timing | Storage on briefly | Save needed data |
| Communicate | Run | Idle/event timing | On briefly | Send data/config |
| Fault recovery | Run | Reset/reinit | Optional | Recover device fault |

## Stop 2 as default

Stop 2 is the best default idle mode because:

- SRAM is retained.
- RTC can remain active.
- LPTIM1 can be active.
- EXTI wakeup is available.
- Current is very low compared with run/sleep.

The datasheet table for Stop 2 current shows around 1 uA typical at 1.8 V / 25 degC without RTC, with values rising with voltage and temperature. Board-level current will be higher because of regulators, pull-ups, MAX35103/MAX35101, sensors and communication modules.

## Wake sources

| Wake source | Recommended use |
|---|---|
| PC13 EXTI | MAX35103/MAX35101 `INT` |
| RTC wakeup timer | Periodic housekeeping / scheduled communication |
| LPTIM1 | Short low-power timeouts |
| LPUART1 | Optional low-power serial wakeup |
| IWDG | Fault recovery |

## GPIO state before Stop 2

Before entering Stop 2:

- CE should be inactive high.
- RST should remain deasserted high unless powering down MAX is intended.
- INT should remain input with pull-up.
- WDO should remain input with pull-up if used.
- Unused GPIOs should be analog/no-pull.
- External modules should be disabled or placed into their own sleep modes.

## Peripheral clock policy

| Peripheral | Before Stop 2 |
|---|---|
| SPI1 | Disable if not needed for wake |
| USART2 debug | Disable or keep only during debug builds |
| I2C | Disable unless a wake-capable bus is deliberately used |
| ADC | Disable |
| DMA | Disable unless required |
| RTC | Keep active if LSE scheduling is used |
| LPTIM1 | Keep active only if needed |
| GPIO/EXTI | Keep wake pins configured |

## Measurement duty cycle example

```text
Every 500 ms or 1 s:
    MAX event timing produces TOF_DIFF result
    MAX asserts INT
    STM32 wakes for 2-10 ms
    STM32 reads and processes
    STM32 returns to Stop 2
```

Communication can be less frequent:

```text
Every 1 min / 5 min / 15 min:
    wake
    package accumulated result
    enable communication module
    transmit
    disable communication module
    return to Stop 2
```

## Calibration low-power policy

Do not run heavy calibration continuously.

Recommended:

- Apply lightweight compensation on every valid sample.
- Run zero-flow recalibration only when a reliable zero-flow condition is detected.
- Run deeper diagnostics on a slower schedule.
- Store calibration only when it changes meaningfully.

## Pull-up current warning

Open-drain signals such as MAX `INT` and `WDO` need pull-ups. Pull-up values affect current:

```text
Strong pull-up  = faster edge, more current when low
Weak pull-up    = lower current, slower edge, more noise-sensitive
```

For low-power product hardware, prefer external pull-ups sized from real measurements. Internal pull-ups are convenient for prototype but not always optimal for battery life.

## Battery measurement policy

Use ADC battery measurement sparingly:

- Enable divider only during measurement, if possible.
- Use high-value divider plus capacitor if leakage budget allows.
- Use VREFINT calibration to estimate VDDA when needed.

## Current measurement plan

Measure these states independently:

1. Board power off leakage.
2. MCU Stop 2 only.
3. MCU Stop 2 + MAX event timing.
4. Wake and SPI readout.
5. Processing burst.
6. Storage write.
7. Communication transmit.
8. Fault/retry state.

Average current is dominated by duty cycle, not peak current alone.

## Low-power conclusion

The best strategy is **Stop 2 + MAX interrupt wake + short bounded processing bursts**. Avoid polling, switch off external modules aggressively, and validate current on real PCB early.
