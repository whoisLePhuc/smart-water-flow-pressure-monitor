# MAX35103 Firmware-Oriented Datasheet Notes

This folder contains reviewed, firmware-oriented notes derived from the official [MAX35103 Rev 2 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max35103.pdf), February 2026.

The notes are intended to turn the datasheet into implementation guidance for an STM32 driver and ultrasonic water-meter measurement pipeline. They do not replace the datasheet's electrical limits, timing tables, package information, or final bitfield authority.

## Files

| File | Purpose |
|---|---|
| `MAX35103_Technical_Summary.md` | High-level technical summary for hardware/firmware architecture |
| `MAX35103_Register_Notes.md` | Register map, important bitfields, TOF/event/calibration notes |
| `MAX35103_SPI_Command_Notes.md` | SPI protocol, opcodes, command workflows, pseudocode |

The source PDF is kept beside these notes. Known ambiguities and verification requirements are recorded directly in `MAX35103_Register_Notes.md` and `MAX35103_SPI_Command_Notes.md`; there is no separate check-report file in this document set.

## Recommended Reading Order

1. Read this README for scope and nonnegotiable implementation rules.
2. Read `MAX35103_Technical_Summary.md` for the measurement model and subsystem boundaries.
3. Read `MAX35103_Register_Notes.md` before defining register constants or configuration profiles.
4. Read `MAX35103_SPI_Command_Notes.md` before implementing transactions, interrupts, event timing, or flash access.
5. Use the official PDF for final electrical, timing, and bitfield validation.

## Recommended Use

Use these files as driver-development notes, not as a replacement for the official datasheet.

Suggested firmware module layout:

```text
drivers/
└── max35103/
    ├── max35103.h
    ├── max35103_spi.c
    ├── max35103_power.c
    ├── max35103_commands.c
    ├── max35103_registers.h
    ├── max35103_tof.c
    ├── max35103_temp.c
    ├── max35103_event.c
    ├── max35103_calib.c
    └── max35103_flash.c
```

Recommended service boundary:

```text
MAX35103 driver
  → raw TOF/temperature/calibration results + status evidence
Measurement manager
  → validates snapshot and measurement sequencing
Flow computation service
  → converts accepted TOF data to velocity/flow
Calibration service
  → zero-flow offset, meter factor, temperature compensation
Volume accumulator
  → accumulates only accepted calibrated samples
```

Do not place hydraulic calibration tables, billing-volume policy, or leak-detection decisions inside the IC driver.

## Driver Bring-Up Order

```text
1. SPI read/write sanity check
2. Read Interrupt Status FEh after power-on
3. Write/read TOF1 and TOF2
4. Select an existing stored configuration or build a new validated register image
5. If the image is new, transfer it to configuration flash and verify completion
6. Initialize; remember that this recalls configuration from flash
7. Calibrate and validate the calibration result
8. Run TOF_UP and TOF_DOWN
9. Run TOF_DIFF and validate signed conversion/error sentinels
10. Enable event timing mode and validate cycle counts/range evidence
11. Add zero-flow calibration, filtering, temperature compensation, and flow calculation above the driver
12. Validate recovery, HALT, timeout, reset, flash, and low-power behavior
```

## Important Notes

- MAX35103 is similar to MAX35101 but lower power and includes `8XS` event timing acceleration.
- SPI is MSB-first mode 1: idle-low SCK, DIN latched on the falling edge (`CPOL=0`, `CPHA=1`).
- `INT` and `WDO` are active-low open-drain outputs and need pull-ups.
- Use the detailed register tables as firmware authority when the high-level memory map or OCR text appears ambiguous.
- For `TOF2`, write reserved bit 3 as `0`.
- Reading Interrupt Status `FEh` clears all flags. One driver component must own that read and dispatch a cached status snapshot.
- `Initialize` reloads configuration from flash; it can overwrite unsaved volatile configuration edits.
- User flash is word-organized, uses even addresses, writes one 16-bit word per command, and erases in 256-byte blocks.
- Configuration flash used by opcode `06h` is separate from the 8 KB user flash.
- TOF/temperature event averages contain only valid cycles. Always check cycle count before using an average.
- Validate SPI mode, CE boundaries, bit order, and DOUT behavior using a logic analyzer.
- Tune TOF delay, comparator offsets, wave selections, and timeout on the real acoustic path.

## Datasheet Ambiguities That Must Stay Visible

| Topic | Conflict | Project rule |
|---|---|---|
| `TOF2` bit 3 | Detailed table marks it reserved; other layouts can be misread | Write `0` |
| 3-hit current example | Current table shows `STOP=101`; detailed TOF2 table maps 3 hits to `010` | Use `010` for 3 hits |
| Control arm flags | Memory map/read-write labels conflict with required write-zero re-arm behavior | Isolate feature; require vendor/reference-driver/bench confirmation |
| `CAL_PERIOD` count | Detailed table says `1 + field`; prose example describes field 6 as six measurements | Verify chosen semantics before production |
| Event flowcharts | Some artwork still names MAX35101 | Follow MAX35103 command descriptions and register tables |

Do not delete these notes merely because one configuration appears to work on a single prototype.

## Configuration Ownership

A configuration profile should explicitly contain:

- `TOF1` through `TOF7`;
- TOF measurement delay;
- Event Timing 1 and 2;
- Calibration and Control;
- RTC control only when the product uses RTC/alarm/watchdog features;
- profile version and CRC at the product level;
- transducer/acoustic-path identity and the calibration dataset that justified the values.

Reserved bits must be masked to zero before every write. Configuration readback should compare only documented fields.

## Minimum Definition of Done

A driver is not complete until it demonstrates:

- deterministic SPI framing and bounded timeouts;
- correct positive and negative signed `TOF_DIFF` conversion;
- one-owner, self-clearing interrupt-status handling;
- rejection of TOF and temperature error sentinels;
- event average validation with cycle count and `TOF_Range`;
- correct configuration-flash/Initialize lifecycle;
- controlled HALT and reset recovery;
- user-flash address/block/write semantics if user flash is enabled;
- known-flow sign verification on the assembled meter;
- tests with a mock/emulator plus logic-analyzer validation on hardware.

## Source Revision Policy

These notes were reviewed against MAX35103 datasheet Rev 2 dated February 2026. When Analog Devices publishes a newer revision:

1. Compare the revision history and affected tables.
2. Re-check every item in the ambiguity table.
3. Update metadata in all three notes.
4. Run register-definition and conversion-vector tests again.
5. Record any behavioral change before updating a production configuration profile.
