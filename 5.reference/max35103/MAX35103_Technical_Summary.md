---
title: MAX35103 Technical Summary
source_pdf: max35103.pdf, Rev 2, February 2026
purpose: Firmware-oriented technical summary for ultrasonic water/heat meter design
status: Reviewed against the official Rev 2 PDF datasheet
device: MAX35103
---

# MAX35103 Technical Summary

> **Source of truth:** [MAX35103 Rev 2 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max35103.pdf), February 2026. This document is an implementation guide, not a replacement for the electrical limits, timing diagrams, or detailed register tables in the datasheet.

Related notes:

- [`MAX35103_Register_Notes.md`](MAX35103_Register_Notes.md): register map, bitfields, result validity, and datasheet inconsistencies.
- [`MAX35103_SPI_Command_Notes.md`](MAX35103_SPI_Command_Notes.md): SPI mode, transactions, commands, flash access, and driver workflows.

## 1. What the MAX35103 Does

The MAX35103 is a reduced-power time-to-digital converter (TDC) with an integrated analog front-end (AFE), real-time clock (RTC), temperature measurement interface, and 8 KB user flash. It targets ultrasonic heat meters, ultrasonic water meters, and ultrasonic gas meters.

For firmware, the main idea is:

```text
MCU configures MAX35103 over SPI
→ MAX35103 launches ultrasonic pulses
→ MAX35103 detects received acoustic hits
→ MAX35103 calculates TOF/TOF_DIFF/averages
→ MAX35103 asserts INT
→ MCU reads result registers and applies metrology algorithms
```

The MCU does not need to timestamp the acoustic waveform directly. The MCU is responsible for configuration, calibration policy, result validation, flow calculation, temperature compensation, data logging, communications, and power management.

## 2. Relationship to MAX35101

The MAX35103 is very similar to the MAX35101 from a firmware architecture point of view, but it is optimized for lower average power.

Key differences to keep in mind:

| Area | MAX35103 note |
|---|---|
| Average power | Lower than MAX35101. Typical device current drain is 5.5 µA for TOF_DIFF = 2/s with 3 hits and temperature = 1/30 s; 7.0 µA with 6 hits. |
| Event timing interval | Adds `8XS` control in Event Timing 1. When set, the configured TOF and temperature event intervals are divided by 8. |
| Package options | 32-pin TQFP 5 mm x 5 mm and 32-pin TQFN 4 mm x 4 mm. |
| Firmware reuse | A MAX35101 driver architecture can be reused, but register definitions and datasheet notes must be checked for MAX35103-specific details. |

## 3. Key Values for Hardware Design

| Item | Value / note |
|---|---|
| Supply voltage | 2.3 V to 3.6 V |
| Operating temperature | -40 °C to +85 °C |
| TOF measurement range | 8 µs to 8000 µs |
| Differential time accuracy | 20 ps |
| Time measurement resolution | 3.8 ps |
| SPI maximum SCK | 20 MHz at VCC >= 3.0 V; 10 MHz at VCC = 2.3 V |
| SPI format | Four-wire, MSB first, idle clock low, DIN latched on falling edge: `CPOL=0`, `CPHA=1` (SPI mode 1) |
| High-speed clock | 4 MHz crystal or ceramic resonator |
| RTC clock | 32.768 kHz crystal or external CMOS 32.768 kHz source |
| RTD support | Up to four 2-wire PT1000/PT500 sensors |
| User flash | 8 KB |
| Packages | 32-TQFP 5 mm x 5 mm; 32-TQFN 4 mm x 4 mm |

## 4. Important Pin Groups

| Group | Pins | Firmware/hardware meaning |
|---|---|---|
| Power | `VCC`, `GND`, `BYPASS`, exposed pad | Connect all supply/ground pins. `BYPASS` requires 100 nF to ground with effective series resistance in the 1 Ω to 2 Ω range. Connect the exposed pad to ground. |
| SPI | `CE`, `SCK`, `DIN`, `DOUT` | Opcode/register/flash interface to host MCU. |
| Interrupt/status | `INT`, `WDO`, `CSW`, `RST` | `INT` and `WDO` are active-low open-drain outputs and require pull-ups. `CSW` is active high. `RST` is active low. |
| Ultrasonic launch | `LAUNCH_UP`, `LAUNCH_DN` | Pulse outputs for upstream/downstream acoustic paths. |
| Ultrasonic receive | `STOP_UP`, `STOP_DN` | Analog STOP inputs from the receive transducer path. |
| Direction/comparator output | `CMP_OUT/UP_DN` | Can output launch direction or comparator output depending on control bits. |
| Temperature | `T1`, `T2`, `T3`, `T4`, `TC` | RTD timing measurement network. |
| Clocks | `X1`, `X2`, `32KX0`, `32KX1`, `32KOUT` | 4 MHz and 32.768 kHz oscillator pins. |

## 5. Measurement Direction

### `TOF_UP`

`TOF_UP` launches pulses from `LAUNCH_UP` and receives on `STOP_UP`. In the datasheet's physical arrangement, `LAUNCH_UP` drives the downstream transducer and the received signal reaches the upstream transducer connected to `STOP_UP`.

### `TOF_DOWN`

`TOF_DOWN` launches pulses from `LAUNCH_DN` and receives on `STOP_DN`. In the datasheet's physical arrangement, `LAUNCH_DN` drives the upstream transducer and the received signal reaches the downstream transducer connected to `STOP_DN`.

### `TOF_DIFF`

`TOF_DIFF` performs back-to-back upstream and downstream measurements. It then stores the differential result:

```text
TOF_DIFF = AVGUP - AVGDN
```

For a water meter, the differential value is normally the useful quantity. Absolute TOF contains fixed delays from the circuit, transducers, and acoustic path mounting.

The words `UP` and `DOWN` must be treated as the MAX35103 measurement-channel convention. Firmware should verify the final flow sign using a known physical flow direction instead of assuming that a positive `TOF_DIFF` always means forward flow for every PCB and transducer wiring arrangement.

## 6. Single TOF Measurement Sequence

A single TOF measurement is handled internally by the MAX35103:

1. Enable the 4 MHz oscillator and internal LDO, then wait for `CLK_S[2:0]` settling time.
2. Apply common-mode bias to the selected STOP pin for the `CT[1:0]` bias charge time.
3. Launch the programmed pulse train using `PL[7:0]` and `DPL[3:0]` in `TOF1`.
4. Wait for `TOF Measurement Delay` before enabling the receiver/hit detector.
5. Detect the early edge with the configured comparator offset.
6. Automatically switch to the comparator return offset.
7. Measure the selected `t2` wave.
8. Capture the configured stop hits.
9. Average the selected hit measurements and compute wave ratios.
10. Set interrupt status flags and assert `INT` if enabled.

Firmware should therefore focus on selecting good wave windows, offsets, timeouts, and validation logic.

### 6.1 Measurement validity before conversion

Do not convert result words directly to time until status and sentinel values have been checked:

- A TOF error causes the associated result registers to report `FFFFh`.
- During a failed `TOF_DIFF`, `TOF_DIFFInt` reports `7FFFh` and `TOF_DIFFFrac` reports `FFFFh`.
- A timeout also sets `TO` in the self-clearing Interrupt Status register.
- Event averages exclude failed measurements, so `TOF_Cycle_Count` can be smaller than the requested `1 + TDM` count.
- `TOF_Cycle_Count == 0` means the averaged result must not be used.

The interrupt/status snapshot, cycle count, and result block should be returned together to the measurement service so validity is not lost between driver calls.

## 7. Launch Frequency

The pulse launcher uses the 4 MHz reference. Internally, the reference is divided to produce a 2 MHz base for pulse launch division.

```text
Pulse Launch Frequency = 2 MHz / (1 + DPL[3:0])
```

Examples:

| `DPL[3:0]` | Launch frequency |
|---:|---:|
| `0001b` | 1 MHz |
| `0010b` | 666 kHz |
| `0011b` | 500 kHz |
| `0100b` | 400 kHz |
| `0111b` | 250 kHz |
| `1110b` | 133.33 kHz |
| `1111b` | 125 kHz |

`DPL = 0000b` is reserved and should not be programmed.

The launch frequency must be selected from the transducer center frequency and the complete acoustic path. Pipe diameter alone is not enough to choose it. Values such as 500 kHz or 1 MHz are engineering candidates to evaluate on the real assembly, not MAX35103 defaults.

## 8. TOF Result Format

Time results are based on the 4 MHz clock.

```text
T4MHz = 1 / 4 MHz = 250 ns
integer LSB = 250 ns
fractional LSB = 250 ns / 65536 ≈ 3.814 ps
```

For unsigned absolute-style result registers:

```c
double max35103_time_seconds(uint16_t int_part, uint16_t frac_part)
{
    const double t4m = 1.0 / 4000000.0;
    return ((double)int_part + (double)frac_part / 65536.0) * t4m;
}
```

The integer word is a 15-bit nonnegative fixed-point integer for normal absolute hit, average, temperature, and calibration results. The corresponding register pair must first be checked for an error sentinel.

For `TOF_DIFF`, treat the concatenated integer/fractional value as signed two's-complement fixed point:

```c
double max35103_tof_diff_seconds(uint16_t int_part, uint16_t frac_part)
{
    int64_t raw = ((int64_t)int_part << 16) | frac_part;

    if (raw & 0x80000000LL) {
        raw -= 0x100000000LL;
    }

    const double t4m = 1.0 / 4000000.0;
    return ((double)raw / 65536.0) * t4m;
}
```

The signed format applies to both `TOF_DIFF` and `TOF_DIFF_AVG`. The two 16-bit words form one signed Q16 fixed-point count of 4 MHz periods; sign extension must cover the complete 32-bit concatenated value.

### 8.1 Event timing diagnostic register

`TOF_Cycle_Count` at `E4h` contains two independent 8-bit fields:

- Bits 15:8: `TOF_Range`, a spread/range diagnostic for the valid differential measurements.
- Bits 7:0: number of valid, error-free `TOF_DIFF` cycles accumulated into `TOF_DIFF_AVG`.

The maximum `TOF_Range` span equals two launch periods, and its resolution is that span divided by 256. Firmware can use it as a signal-stability diagnostic, but it is not a substitute for the individual hit checks or project-level quality model.

## 9. Temperature Measurement Concept

Temperature measurement is based on RC timing through the selected `T1`-`T4` ports and `TC`.

A common ratiometric setup is:

```text
T1 / T3 = R_RTD1 / R_REF
T2 / T4 = R_RTD2 / R_REF
```

The MCU should convert timing ratios to RTD resistance, then use a PT1000/PT500 lookup table or interpolation to obtain temperature.

Temperature error handling:

| Condition | Result behavior |
|---|---|
| Short-circuit probe | Corresponding result register reports `0000h`. |
| Open-circuit probe | Corresponding result register reports `FFFFh`; timeout interrupt can be set. |
| Other temperature error | Temperature result registers can be marked invalid with `FFFFh`. |

The short-circuit threshold is a measured time below 8 µs. Open-circuit detection is tied to the selected `PORTCYC` timeout. Invalid temperature cycles are not accumulated into the averaged temperature results, so `Temp_Cycle_Count` may be smaller than `1 + TMM` and must be checked before using `Tx_AVG`.

## 10. Calibration Concept

MAX35103 can calibrate the TDC using the 32.768 kHz oscillator as the accurate reference. This is especially useful when a 4 MHz ceramic resonator is used instead of a crystal.

Calibration can be triggered in two ways:

| Mode | Behavior |
|---|---|
| Manual `Calibrate` command | Updates calibration result registers and sets the `CAL` interrupt bit. |
| Automatic during event timing | Configured with `CAL_CFG[2:0]`; updates calibration registers but does not set the `CAL` interrupt bit. |

The MCU can use this calibration result as part of the measurement scaling strategy. If a calibration error occurs, the device keeps the previous calibration data instead of overwriting it with invalid data.

For a nominal 4 MHz clock and one 32.768 kHz period, the expected count is:

```text
C_expected = 4,000,000 / 32,768 = 122.0703125
gain = C_expected / C_measured
```

When `CAL_USE=1`, the MAX35103 applies the calibration data during measurement, averaging, and accumulation. The host should not apply the same gain a second time. If firmware performs scaling itself with `CAL_USE=0`, it must define one tested fixed-point convention and record whether the reported value is raw or calibrated.

The detailed `CAL_PERIOD` table says the number of 32.768 kHz cycles is `1 + CAL_PERIOD`, while an explanatory example in the same datasheet describes a field value of 6 as six measurements. This internal inconsistency is recorded in the register notes and should be resolved with a controlled bench test or vendor clarification before relying on nondefault calibration-period semantics.

## 11. Event Timing Mode

Event timing mode lets the MAX35103 run cyclic measurements while the MCU sleeps.

| Command | Opcode | Function |
|---|---:|---|
| `EVTMG1` | `07h` | Automatic `TOF_DIFF` plus temperature sequence |
| `EVTMG2` | `08h` | Automatic `TOF_DIFF` sequence |
| `EVTMG3` | `09h` | Automatic temperature sequence |

Important event bits:

| Bit | Register | Meaning |
|---|---|---|
| `8XS` | Event Timing 1 | Increases TOF and temperature event timing rate by 8x. |
| `TDF[3:0]` | Event Timing 1 | Base TOF_DIFF period selection. |
| `TDM[4:0]` | Event Timing 1 | Number of TOF_DIFF cycles in one sequence. |
| `TMF[5:0]` | Event Timing 1 | Base temperature measurement period selection. |
| `TMM[4:0]` | Event Timing 2 | Number of temperature cycles in one sequence. |
| `CAL_USE` | Event Timing 2 | Use calibration data during measurement/averaging. |
| `CAL_CFG[2:0]` | Event Timing 2 | Select when automatic calibration is run. |
| `INT_EN` | Calibration and Control | Enables `INT` pin assertion. |
| `ET_CONT` | Calibration and Control | Keeps EVTMG running continuously until `HALT`. |
| `CONT_INT` | Calibration and Control | Interrupts after every measurement cycle. |

Important behavior:

- `ET_CONT=0`: execute one configured sequence and stop.
- `ET_CONT=1`: repeat sequences until `HALT` completes.
- `CONT_INT=1`: request an interrupt after each individual TOF or temperature cycle.
- `CONT_INT=0`: normally wake the MCU at sequence completion.
- Event-timing errors do not terminate the event engine; invalid cycles are excluded from the average and the engine continues.
- `HALT` is not immediate. The device finishes the currently executing TOF or temperature operation, freezes the result set, then sets `HALT` status.

The `8XS` bit changes the interval, not the programmed sequence count. For example, `TDF=0` selects 0.5 s normally and 0.0625 s with `8XS=1`.

## 12. Boundary Between MAX35103 and Flow Computation

The MAX35103 returns acoustic timing information; it does not directly return volumetric flow. A typical MCU-side computation is:

```text
delta_t = corrected_t_up - corrected_t_down
velocity = L / (2 * cos(theta)) * (1 / t_up - 1 / t_down)
flow = velocity * effective_area * meter_factor
```

The exact equation and sign depend on the meter geometry and channel convention. Production firmware also needs:

- zero-flow differential offset removal;
- temperature-dependent sound-speed and fluid-property compensation;
- meter-factor or calibration-table application;
- forward/reverse-flow policy;
- bubble, empty-pipe, weak-signal, timeout, and outlier rejection;
- volume accumulation only from accepted samples.

Keep these algorithms above the MAX35103 driver. The driver should expose raw results plus evidence; the measurement/calibration services decide whether a sample is suitable for billing, diagnostics, or neither.

## 13. Recommended Firmware Architecture

```text
max35103/
├── max35103_spi.c          # CE control, SPI transfer, command/register/flash access
├── max35103_registers.h    # opcodes, register addresses, bit fields
├── max35103_commands.c     # Reset, Initialize, TOF_DIFF, EVTMGx, HALT, Calibrate
├── max35103_tof.c          # TOF config, result conversion, hit validation
├── max35103_temp.c         # RTD timing ratio and temperature conversion helpers
├── max35103_event.c        # event timing mode and interrupt workflow
├── max35103_calib.c        # calibration use, zero-flow offset, drift tracking
└── max35103.h              # public API
```

The driver should not own the product's zero-flow calibration table, hydraulic model, or billing volume. Those belong to higher-level services so the same driver can be tested against a simulator and reused across meter geometries.

Suggested public API:

```c
bool max35103_reset(max35103_t *dev);
bool max35103_initialize(max35103_t *dev);
bool max35103_write_reg(max35103_t *dev, uint8_t write_opcode, uint16_t value);
bool max35103_read_reg(max35103_t *dev, uint8_t read_opcode, uint16_t *value);
bool max35103_start_tof_diff(max35103_t *dev);
bool max35103_read_tof_diff(max35103_t *dev, max35103_time_result_t *out);
bool max35103_start_event_mode(max35103_t *dev, max35103_event_mode_t mode);
bool max35103_calibrate(max35103_t *dev);
```

Prefer a result API that preserves evidence:

```c
typedef struct {
    uint16_t interrupt_status;
    uint16_t wave_ratio_up;
    uint16_t wave_ratio_down;
    uint8_t valid_cycle_count;
    uint8_t tof_range;
    int32_t tof_diff_q16;
    int32_t tof_diff_avg_q16;
    bool valid;
} max35103_tof_sample_t;
```

## 14. Bring-Up Checklist

1. Verify 3.3 V rail and decoupling.
2. Verify 4 MHz and 32.768 kHz oscillator operation.
3. Check `RST`, `CE`, `SCK`, `DIN`, `DOUT`, and `INT` with a logic analyzer.
4. Read `Interrupt Status` after power-on and confirm `POR`.
5. Decide whether the run uses the configuration already stored in configuration flash or a new configuration.
6. For a new configuration, write `TOF1`-`TOF7`, delay, event, and control registers, then execute `Transfer Configuration to Flash` and verify completion. `Initialize` recalls configuration from flash, so unsaved volatile edits must not be assumed to survive it.
7. Send `Initialize` and wait until the device makes SPI available again; verify completion status.
8. Run `Calibrate`.
9. Run `TOF_UP`, `TOF_DOWN`, then `TOF_DIFF`.
10. Validate `AVGUP`, `AVGDN`, `TOF_DIFF`, `TOF_Cycle_Count`, and timeout flags.
11. Tune `DPL`, `PL`, `STOP`, `T2WV`, `HITxWV`, `DLY`, and comparator offsets based on real signal behavior.
12. Confirm `INT` and `WDO` pull-ups, timeout behavior, HALT latency, reset recovery, and invalid-result sentinels.
13. Run a known-direction flow test and record the sign mapping between `TOF_DIFF` and forward/reverse flow.
