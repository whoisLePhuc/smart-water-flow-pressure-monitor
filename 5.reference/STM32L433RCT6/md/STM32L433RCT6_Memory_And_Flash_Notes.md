<!--
Source PDF: STM32L433xx datasheet, DS11449 Rev 8, July 2024.
Target device: STM32L433RCT6.
Project context: ultrasonic water meter using MAX35103/MAX35101.
These notes are engineering planning notes, not a replacement for the official datasheet, RM0394, ES0318, or application notes.
-->
# STM32L433RCT6 Memory and Flash Notes

## Memory summary

| Memory | Size | Notes |
|---|---:|---|
| Flash | 256 KB | Single-bank, 128 pages x 2 KB |
| SRAM1 | 48 KB | Main SRAM at `0x20000000` |
| SRAM2 | 16 KB | At `0x10000000`, also aliased at `0x2000C000`; hardware parity; can be retained in Standby |
| Backup registers | 128 bytes | 32 x 32-bit registers, VBAT domain |
| System memory | ST bootloader | USART/I2C/SPI/CAN/USB DFU support depends on bootloader details |

## Important implications

`STM32L433RCT6` is much more constrained than STM32L476RG:

| Item | STM32L433RCT6 | STM32L476RG |
|---|---:|---:|
| Flash | 256 KB | 1 MB |
| SRAM | 64 KB | 128 KB |

This does not block the project, but it changes firmware strategy.

## Flash layout proposal without OTA

```text
0x08000000
├── Vector table + startup
├── Boot/application metadata
├── Application firmware
├── Factory default configuration
├── Backup calibration block A
├── Backup calibration block B
└── Reserved safety margin
0x08040000  end of 256 KB flash
```

Use external EEPROM/FRAM for data that changes often.

## Flash layout proposal with small bootloader

```text
0x08000000
├── Bootloader, 16-32 KB
├── Application firmware
├── Factory defaults
├── Calibration backup A/B
└── Reserved margin
0x08040000
```

This is possible, but leaves less room for application firmware. Full dual-image OTA in internal flash is not practical.

## Internal flash write policy

Use internal flash only for rarely changed data:

- Factory calibration defaults.
- Device identity/configuration mirror.
- Last-known-good small calibration block.
- Firmware metadata.

Avoid internal flash for:

- High-rate measurement logs.
- Frequent zero-flow history updates.
- Large diagnostic history.
- Continuous metering records.

## External storage recommendation

| Storage | Use |
|---|---|
| I2C EEPROM | Low-cost configuration/calibration storage with limited write cycles |
| I2C/SPI FRAM | Best for frequent calibration/history/log updates |
| SPI/QSPI flash | Large logs or OTA staging |

For this water meter project, **FRAM is preferred** if you plan to store runtime calibration history frequently.

## SRAM budget proposal

| Area | Suggested budget |
|---|---:|
| Main stack | 4-8 KB |
| ISR/RTOS stacks | 4-16 KB depending on RTOS/task count |
| MAX35103 driver state | < 1 KB |
| Measurement window/filter buffers | 2-8 KB |
| Calibration manager | 2-8 KB |
| Communication buffers | 2-12 KB |
| Logging buffers | 1-8 KB |
| Safety margin | > 8 KB |

## RTOS consideration

FreeRTOS can be used, but with 64 KB SRAM it must be configured carefully.

Recommended if using RTOS:

```text
Task: metrology       stack 2-4 KB
Task: communication   stack 2-4 KB
Task: storage         stack 1-2 KB
Task: diagnostics     stack 1-2 KB
Idle/timer task       keep small
```

Avoid creating many tasks. An event-driven superloop is also acceptable and may be better for the first product.

## Calibration data design

Use compact fixed-size records:

```c
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    float zero_offset_ns;
    float temp_coeff_a;
    float temp_coeff_b;
    float k_factor;
    uint32_t crc32;
} meter_calib_block_t;
```

For larger flow calibration tables, store them externally.

## Backup register use

Use backup registers for tiny state only:

- Boot counter.
- Last reset reason.
- Last valid flow state summary.
- Calibration validity flag.
- Brownout/fault marker.

Do not store calibration tables in backup registers.

## SRAM2 use

SRAM2 can be used for retained state or fault breadcrumbs:

- Last measurement result.
- Last error code.
- Watchdog breadcrumb.
- Compact state needed after Standby.

Keep it small and deterministic.

## Memory conclusion

The MCU is suitable if firmware is written with embedded discipline. The key rule is:

```text
Internal Flash = firmware + small persistent config
Internal SRAM  = short live buffers only
External NVM   = logs, calibration history, large tables
```
