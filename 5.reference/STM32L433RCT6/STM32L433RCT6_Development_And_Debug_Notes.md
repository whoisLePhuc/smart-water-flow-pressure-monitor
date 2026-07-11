<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Development and Debug Notes

## Toolchain

Recommended development setup:

| Tool | Use |
|---|---|
| STM32CubeMX / STM32CubeIDE | Clock, pin, HAL/LL generation |
| arm-none-eabi-gcc / CMake | Reproducible build |
| ST-LINK/V3 or ST-LINK/V2 | SWD programming/debug |
| Logic analyzer | SPI/INT/CE verification |
| UART-USB adapter | Debug console on PA2/PA3 |
| Current meter / source meter | Low-power validation |

## SWD pins

Reserve:

| Pin | Function |
|---|---|
| PA13 | SWDIO |
| PA14 | SWCLK |
| NRST | Reset |
| GND | Probe ground |
| 3V3 | Target voltage sense |

Keep SWD connected in all prototype revisions.

## Debug UART

Use:

```text
PA2 = USART2_TX
PA3 = USART2_RX
115200 baud, 8N1
```

Do not use PD5/PD6 on LQFP64.

## Bring-up checklist

### Power and reset

- Check 3.3 V rail.
- Check VDDA/VDDUSB/VBAT handling.
- Check NRST level.
- Check BOOT0 state.
- Confirm SWD connection.

### Basic firmware

- Flash a minimal blink program.
- Print boot message through USART2.
- Print reset reason.
- Confirm system clock.
- Confirm RTC/LSE if assembled.

### MAX35103/MAX35101 interface

- Hold CE high at idle.
- Toggle RST and verify with scope.
- Read a known register/status through SPI.
- Validate SPI mode with logic analyzer.
- Trigger a manual measurement.
- Confirm INT falling edge.
- Read interrupt/status/result registers.

### Low-power

- Enter Stop 2.
- Wake by EXTI on PC13.
- Wake by RTC.
- Measure current in Stop 2.
- Measure current during processing burst.
- Confirm debug build does not hide product current issues.

## Logging policy

Use multiple log levels:

```c
typedef enum {
    LOG_ERROR,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG,
    LOG_TRACE,
} log_level_t;
```

Compile out `DEBUG` and `TRACE` for production.

## Hardware test commands

Provide a simple UART CLI in prototype firmware:

```text
help
max reset
max read <addr>
max write <addr> <value>
max tof_up
max tof_dn
max tof_diff
cal zero_start
cal zero_stop
stats
sleep stop2
```

Keep this CLI out of release firmware if flash becomes tight.

## Logic analyzer signals

Capture:

| Signal | Reason |
|---|---|
| SPI1_SCK | SPI clock verification |
| SPI1_MOSI | Opcode/write data |
| SPI1_MISO | Read data |
| CE | Frame boundaries |
| INT | Measurement completion |
| RST | Reset sequence |
| USART2_TX | Debug timing correlation |

## Unit and hardware tests

### Unit tests on host

- Flow calculation.
- Temperature compensation.
- Calibration table interpolation.
- CRC/config validation.
- Filter/outlier logic.

### Hardware tests on target

- GPIO init state.
- SPI register read/write.
- MAX reset/reinit.
- INT EXTI wake.
- RTC wake.
- Stop 2 current.
- Fault recovery.

## Debug conclusion

Use UART and logic analyzer heavily during early driver work, but always re-test low-power current with debug logs and debugger detached. SWD and UART are for development; the final metering loop should be interrupt-driven and mostly asleep.
