---
title: MAX35103 Register Notes
source_pdf: max35103.pdf, Rev 2, February 2026
purpose: PDF-checked register notes for firmware driver development
status: Reviewed against the official Rev 2 PDF datasheet
device: MAX35103
---

# MAX35103 Register Notes

> **Source of truth:** [MAX35103 Rev 2 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max35103.pdf), especially Tables 3 and 5-25. Preserve reserved bits as zero unless a later official erratum says otherwise.

## 1. Register Access Model

MAX35103 uses opcode-based SPI register access.

| Access | Opcode range | Width | Notes |
|---|---:|---:|---|
| Read register | `B0h` to `FFh` | 16-bit | RTC, configuration, result, and status registers |
| Write register | `30h` to `43h` | 16-bit | Writable RTC/config/control registers |
| Read flash | `90h` | 8 KB flash, even addresses |
| Write flash | `10h` | 8 KB flash, even addresses |
| Block erase flash | `13h` | Flash block erase |

Configuration registers are restored from flash after reset. Factory-stored flash defaults are `0000h` for all configuration registers except `TOF1`, which defaults to `0010h`.

The configuration-flash area used by opcode `06h` is separate from the 8 KB user flash addressed by `90h`, `10h`, and `13h`. `Initialize` recalls configuration from configuration flash, so volatile configuration writes made immediately before `Initialize` are not a durable configuration unless they were transferred first.

The datasheet has one special-access ambiguity around the Control register used to re-arm alarm/tamper flags. It is documented in Section 16; do not derive that write opcode from the normal `30h` to `43h` range.

## 2. Writable Register Map

| Read opcode | Write opcode | Name | Group |
|---:|---:|---|---|
| `B0h` | `30h` | `Seconds` | RTC/watchdog |
| `B1h` | `31h` | `Mins_Hrs` | RTC/watchdog |
| `B2h` | `32h` | `Day_Date` | RTC/watchdog |
| `B3h` | `33h` | `Month_Year` | RTC/watchdog |
| `B4h` | `34h` | `Watchdog Alarm Counter` | RTC/watchdog |
| `B5h` | `35h` | `Alarm` | RTC/watchdog |
| `B6h` | `36h` | Reserved | Configuration |
| `B7h` | `37h` | Reserved | Configuration |
| `B8h` | `38h` | `TOF1` | TOF launch/edge/bias |
| `B9h` | `39h` | `TOF2` | TOF hit count, t2, timing, timeout |
| `BAh` | `3Ah` | `TOF3` | HIT1/HIT2 wave select |
| `BBh` | `3Bh` | `TOF4` | HIT3/HIT4 wave select |
| `BCh` | `3Ch` | `TOF5` | HIT5/HIT6 wave select |
| `BDh` | `3Dh` | `TOF6` | Upstream comparator offset |
| `BEh` | `3Eh` | `TOF7` | Downstream comparator offset |
| `BFh` | `3Fh` | `Event Timing 1` | TOF/temperature event rates and counts |
| `C0h` | `40h` | `Event Timing 2` | Temperature count, calibration, RTD port sequence |
| `C1h` | `41h` | `TOF Measurement Delay` | Receiver enable delay |
| `C2h` | `42h` | `Calibration and Control` | Interrupt/event/control/clock settings |
| `C3h` | `43h` | `Real-Time Clock` | RTC oscillator, alarm, watchdog |

## 3. Conversion Result Registers

| Read opcode | Name | Meaning |
|---:|---|---|
| `C4h` | `WVRUP` | Upstream wave ratio result |
| `C5h` | `Hit1UpInt` | Upstream Hit1 integer part |
| `C6h` | `Hit1UpFrac` | Upstream Hit1 fractional part |
| `C7h` | `Hit2UpInt` | Upstream Hit2 integer part |
| `C8h` | `Hit2UpFrac` | Upstream Hit2 fractional part |
| `C9h` | `Hit3UpInt` | Upstream Hit3 integer part |
| `CAh` | `Hit3UpFrac` | Upstream Hit3 fractional part |
| `CBh` | `Hit4UpInt` | Upstream Hit4 integer part |
| `CCh` | `Hit4UpFrac` | Upstream Hit4 fractional part |
| `CDh` | `Hit5UpInt` | Upstream Hit5 integer part |
| `CEh` | `Hit5UpFrac` | Upstream Hit5 fractional part |
| `CFh` | `Hit6UpInt` | Upstream Hit6 integer part |
| `D0h` | `Hit6UpFrac` | Upstream Hit6 fractional part |
| `D1h` | `AVGUPInt` | Upstream average integer part |
| `D2h` | `AVGUPFrac` | Upstream average fractional part |
| `D3h` | `WVRDN` | Downstream wave ratio result |
| `D4h`-`DFh` | `Hit1Dn...Hit6Dn` | Downstream hit integer/fractional results |
| `E0h` | `AVGDNInt` | Downstream average integer part |
| `E1h` | `AVGDNFrac` | Downstream average fractional part |
| `E2h` | `TOF_DIFFInt` | Differential TOF integer part |
| `E3h` | `TOF_DIFFFrac` | Differential TOF fractional part |
| `E4h` | `TOF_Cycle_Count` | Valid TOF event measurement count/range register |
| `E5h` | `TOF_DIFF_AVGInt` | Event average TOF_DIFF integer part |
| `E6h` | `TOF_DIFF_AVGFrac` | Event average TOF_DIFF fractional part |
| `E7h`-`EEh` | `T1...T4 Int/Frac` | Temperature timing results |
| `EFh` | `Temp_Cycle_Count` | Valid temperature event measurement count |
| `F0h`-`F7h` | `T1_AVG...T4_AVG Int/Frac` | Averaged temperature timing results |
| `F8h` | `CalibrationInt` | Calibration integer result |
| `F9h` | `CalibrationFrac` | Calibration fractional result |
| `FAh`-`FDh` | Reserved | Do not use |

`E4h` is a packed diagnostic register:

| Bits | Field | Meaning |
|---:|---|---|
| 15:8 | `TOF_Range` | Range/spread of valid, error-free event-mode `TOF_DIFF` values |
| 7:0 | `TOF_Cycle_Count` | Number of valid cycles accumulated into `TOF_DIFF_AVG` |

For a programmed divider `DPL`, the `TOF_Range` full scale is two launch periods:

```text
range_full_scale_us = DPL + 1
range_lsb_us = range_full_scale_us / 256
```

For example, `DPL=0001b` produces 1 MHz launch pulses, 2 µs full scale, and approximately 7.8125 ns per range count. Treat `TOF_Range` as a stability metric, not as flow or absolute TOF.

## 4. Status Registers

| Read opcode | Name | Notes |
|---:|---|---|
| `FEh` | `Interrupt Status` | Read-only, self-clearing on read |
| `FFh` | `Control` | Read-only control/arm flags |

The memory map labels `FFh` as the read opcode for Control. Its `AFA` and `CSWA` bits are latched arm flags that the detailed description says must be written to zero to re-arm. The write-opcode presentation in Table 23 is internally inconsistent; see Section 16 before implementing a write.

## 5. Result Encoding and Error Sentinels

Normal hit, average, temperature, and calibration results use a pair of registers:

```text
time = (integer + fraction / 65536) * t4MHz
t4MHz = 250 ns for an ideal 4 MHz clock
```

`TOF_DIFF` and `TOF_DIFF_AVG` concatenate their integer and fractional words into a signed 32-bit two's-complement Q16 count of 4 MHz periods.

```c
static inline int32_t max35103_join_signed_q16(uint16_t integer,
                                               uint16_t fraction)
{
    return (int32_t)(((uint32_t)integer << 16) | fraction);
}
```

Check validity before conversion:

| Condition | Register behavior |
|---|---|
| Generic TOF error | Associated result words report `FFFFh` |
| Failed `TOF_DIFF` | `TOF_DIFFInt=7FFFh`, `TOF_DIFFFrac=FFFFh` |
| Temperature short | Corresponding result is `0000h`; short threshold is below 8 µs |
| Temperature open | Corresponding result is `FFFFh`; `TO` is set |
| Event average | Failed cycles are excluded; cycle count may be below the requested count |

Never accept an event average when its valid cycle count is zero. Read the count and average in the same logical snapshot.

## 6. `TOF1` Register - Write `38h`, Read `B8h`

Factory-stored flash value: `0010h`.

| Bits | Name | Meaning |
|---:|---|---|
| 15:8 | `PL[7:0]` | Pulse launcher size. `00h` disables launcher. Up to 127 pulses are launched; if `PL7` is set, count is clamped at 127. |
| 7:4 | `DPL[3:0]` | Pulse launch divider. `0000b` is reserved. |
| 3 | `STOP_POL` | `0` = rising slope detection; `1` = falling slope detection. |
| 2 | `X` | Reserved. Write `0`. |
| 1:0 | `CT[1:0]` | Bias charge time for STOP pin common-mode bias. |

Pulse launch frequency:

```text
f_launch = 2 MHz / (1 + DPL[3:0])
```

Bias charge time:

| `CT[1:0]` | 32 kHz cycles | Typical time |
|---:|---:|---:|
| `00b` | 2 | 61 µs |
| `01b` | 4 | 122 µs |
| `10b` | 8 | 244 µs |
| `11b` | 16 | 488 µs |

## 7. `TOF2` Register - Write `39h`, Read `B9h`

Factory-stored flash value: `0000h`.

| Bits | Name | Meaning |
|---:|---|---|
| 15:13 | `STOP[2:0]` | Number of stop hits to capture. |
| 12:7 | `T2WV[5:0]` | Wave number used for `t2` measurement. |
| 6:4 | `TOF_CYC[2:0]` | Start-to-start delay between `TOF_UP` and `TOF_DN` within `TOF_DIFF`. |
| 3 | `X` | Reserved in the detailed `TOF2` table. Write `0`. |
| 2:0 | `TIMOUT[2:0]` | Timeout window for `t1`, `t2`, and Hit1-Hit6. |

### 7.1 Stop hit count

| `STOP[2:0]` | Captured hits |
|---:|---:|
| `000b` | 1 |
| `001b` | 2 |
| `010b` | 3 |
| `011b` | 4 |
| `100b` | 5 |
| `101b` | 6 |
| `110b` | 6 |
| `111b` | 6 |

### 7.2 `t2` wave selection

| `T2WV[5:0]` decimal | Effective wave |
|---:|---:|
| 0 to 2 | Wave 2 |
| 3 | Wave 3 |
| 4 | Wave 4 |
| 5 to 63 | Wave 5 to Wave 63 |

### 7.3 `TOF_CYC`

| `TOF_CYC[2:0]` | Typical delay | 4 MHz on between `TOF_UP` and `TOF_DOWN` |
|---:|---:|---|
| `000b` | 0 µs programmed gap | Yes |
| `001b` | 122 µs | Yes |
| `010b` | 244 µs | Yes |
| `011b` | 488 µs | Yes |
| `100b` | 732 µs | Yes |
| `101b` | 976 µs | Yes |
| `110b` | 16.65 ms | No |
| `111b` | 19.97 ms | No |

These values configure the scheduled start-to-start spacing used inside `TOF_DIFF`. They do not force two acoustic measurements to overlap: if the active path measurement exceeds the programmed interval, the next direction starts only after the current measurement completes.

### 7.4 Timeout

| `TIMOUT[2:0]` | Timeout |
|---:|---:|
| `000b` | 128 µs |
| `001b` | 256 µs |
| `010b` | 512 µs |
| `011b` | 1024 µs |
| `100b` | 2048 µs |
| `101b` | 4096 µs |
| `110b` | 8192 µs |
| `111b` | 16384 µs |

## 8. `TOF3`, `TOF4`, `TOF5` - Hit Wave Selection

| Register | Write/read | Fields |
|---|---|---|
| `TOF3` | `3Ah` / `BAh` | `HIT1WV[5:0]`, `HIT2WV[5:0]` |
| `TOF4` | `3Bh` / `BBh` | `HIT3WV[5:0]`, `HIT4WV[5:0]` |
| `TOF5` | `3Ch` / `BCh` | `HIT5WV[5:0]`, `HIT6WV[5:0]` |

Rules:

- `HIT1WV` must be at least one wave later than `T2WV`.
- Every following hit must be at least one wave later than the previous hit.
- The earliest effective waves are: Hit1 wave 3, Hit2 wave 4, Hit3 wave 5, Hit4 wave 6, Hit5 wave 7, Hit6 wave 8.

The fields are six bits wide but are separated by reserved bits in each 16-bit register. Use explicit masks/shifts; do not pack them as two adjacent 6-bit values without checking the detailed table.

## 9. `TOF6` and `TOF7` - Comparator Offset Registers

| Register | Write/read | Direction | Fields |
|---|---|---|---|
| `TOF6` | `3Dh` / `BDh` | Upstream | `C_OFFSETUPR[7:0]`, `C_OFFSETUP[6:0]` |
| `TOF7` | `3Eh` / `BEh` | Downstream | `C_OFFSETDNR[7:0]`, `C_OFFSETDN[6:0]` |

Concept:

- Initial comparator offset detects the early edge `t1`.
- Return offset is applied automatically after `t1` for later selected waves.
- Offset LSB scales with VCC: `1 LSB = VCC / 3072`.

The initial offset fields are sign/polarity-dependent 7-bit magnitudes used for the first detectable edge. The return-offset fields are signed 8-bit two's-complement values with a range of +127 to -128 LSB. Do not use the same encoder for both field types.

Firmware should tune these registers empirically on the real acoustic path. For robust metering, store tuned values per product/calibration profile.

## 10. `Event Timing 1` - Write `3Fh`, Read `BFh`

Factory-stored flash value: `0000h`.

| Bits | Name | Meaning |
|---:|---|---|
| 15:12 | `TDF[3:0]` | TOF_DIFF event rate selection. |
| 11:7 | `TDM[4:0]` | Number of TOF_DIFF measurements: `cycles = 1 + TDM`. |
| 6:1 | `TMF[5:0]` | Temperature measurement event rate selection. |
| 0 | `8XS` | If set, event sample rate is increased by 8x. |

Configured intervals:

```text
TOF_DIFF interval with 8XS=0: (TDF + 1) * 0.5 s
TOF_DIFF interval with 8XS=1: (TDF + 1) * 0.0625 s
Temperature interval with 8XS=0: (TMF + 1) * 1.0 s
Temperature interval with 8XS=1: (TMF + 1) * 0.125 s
```

`TDM` requests `1 + TDM` TOF cycles per sequence. `8XS` changes the intervals, not this count.

## 11. `Event Timing 2` - Write `40h`, Read `C0h`

Factory-stored flash value: `0000h`.

| Bits | Name | Meaning |
|---:|---|---|
| 15:11 | `TMM[4:0]` | Number of temperature measurement cycles: `cycles = 1 + TMM`. |
| 10 | `CAL_USE` | Use calibration data during measurement, averaging, and accumulation. |
| 9:7 | `CAL_CFG[2:0]` | Select automatic calibration point in EVTMG sequences. |
| 6:5 | `TP[1:0]` | Temperature port sequence. |
| 4:2 | `PRECYC[2:0]` | Number of dummy/preamble temperature cycles. |
| 1:0 | `PORTCYC[1:0]` | Start-to-start interval between temperature port measurements. |

### 11.1 `CAL_CFG[2:0]`

| `CAL_CFG` | Meaning |
|---:|---|
| `000b` to `011b` | Autocalibration disabled |
| `100b` | Calibrate at beginning of each TOF_DIFF cycle and each temperature cycle |
| `101b` | Calibrate at beginning of each TOF_DIFF cycle and each temperature sequence |
| `110b` | Calibrate at beginning of each TOF_DIFF sequence and each temperature cycle |
| `111b` | Calibrate at beginning of each TOF_DIFF sequence and each temperature sequence |

### 11.2 Temperature port selection

| `TP[1:0]` | Ports measured |
|---:|---|
| `00b` | T1 and T3 |
| `01b` | T2 and T4 |
| `10b` | T1, T3, and T2 |
| `11b` | T1, T3, T2, and T4 |

### 11.3 `PRECYC` and `PORTCYC`

`PRECYC[2:0]` directly selects 0 to 7 dummy cycles. These cycles use the internal RTD-emulation path to reduce timing-capacitor dielectric-absorption effects without repeatedly self-heating the external RTDs.

| `PORTCYC[1:0]` | Typical interval |
|---:|---:|
| `00b` | 128 µs |
| `01b` | 256 µs |
| `10b` | 384 µs |
| `11b` | 512 µs |

`PORTCYC` is both the start-to-start port interval and part of open-circuit timeout behavior. Select it from the complete temperature sequence timing, not only from desired sample rate.

## 12. `TOF Measurement Delay` - Write `41h`, Read `C1h`

| Bits | Name | Meaning |
|---:|---|---|
| 15:0 | `DLY[15:0]` | Receiver enable delay from launch start; units are 4 MHz periods, i.e. 250 ns per count. |

Notes:

- Values below `0012h` are reserved and should not be used.
- Make sure `TIMOUT[2:0]` is long enough so timeout does not occur before the delay expires.
- This is one of the most important registers for avoiding false hits from ringing and early noise.

## 13. `Calibration and Control` - Write `42h`, Read `C2h`

Factory-stored flash value: `0000h`.

| Bits | Name | Meaning |
|---:|---|---|
| 15:12 | `X` | Reserved |
| 11 | `CMP_EN` | Enables `CMP_OUT/UP_DN` output pin |
| 10 | `CMP_SEL` | Selects comparator output vs launch direction output |
| 9 | `INT_EN` | Enables `INT` pin |
| 8 | `ET_CONT` | Event timing continuous operation |
| 7 | `CONT_INT` | Interrupt after every event cycle |
| 6:4 | `CLK_S[2:0]` | 4 MHz oscillator settling time |
| 3:0 | `CAL_PERIOD[3:0]` | Number of 32.768 kHz periods used for calibration |

`CLK_S[2:0]` mapping:

| `CLK_S` | Behavior |
|---:|---|
| `000b` | 16 cycles of 32.768 kHz, approximately 488 µs |
| `001b` | 48 cycles, approximately 1.46 ms |
| `010b` | 96 cycles, approximately 2.93 ms |
| `011b` | 128 cycles, approximately 3.9 ms |
| `100b` | 168 cycles, approximately 5.13 ms |
| `101b`-`111b` | Keep the 4 MHz oscillator on continuously |

The detailed table defines calibration averaging length as `1 + CAL_PERIOD`. An explanatory calibration example elsewhere describes a field value of 6 as six measurements. Because those statements disagree, isolate the field behind a named configuration option and verify the chosen value on hardware or with Analog Devices before relying on an exact nondefault sample-count interpretation.

## 14. `Real-Time Clock and Watchdog Registers`

### 14.1 RTC/calendar data format

`Seconds`, `Mins_Hrs`, `Day_Date`, and `Month_Year` use BCD fields, not binary integers. The calendar includes hundredths/tenths, seconds, minutes, hours, day, date, month, and year. Invalid BCD or illogical date/time values lead to undefined behavior.

The hour format bit is stored in `Mins_Hrs`:

- 24-hour mode: `12/24=0`; the adjacent high-hour bit represents 20 hours.
- 12-hour mode: `12/24=1`; the adjacent bit represents AM/PM.

The alarm register must use the same hour format as `Mins_Hrs`.

### 14.2 Watchdog

The watchdog seed is a 16-bit BCD value in 10 ms units from 0.01 s to 99.99 s. Writing the seed reloads the counter. With `WD_EN=1`, it decrements to zero, sets `WF`, and drives the open-drain `WDO` output low for approximately 250 ms. Clear `WF` by writing it to zero; a zero seed does not assert `WF`.

### 14.3 RTC control at `C3h` / `43h`

| Bit(s) | Name | Meaning |
|---:|---|---|
| 6 | `32K_BP` | Bypass crystal oscillator and accept external 32.768 kHz CMOS source on `32KX1` |
| 5 | `32K_EN` | Enable `32KOUT` |
| 4 | `EOSC` | Active-low oscillator enable: `0` runs RTC oscillator |
| 3:2 | `AM[1:0]` | Alarm mode: none, minute, hour, or hour+minute match |
| 1 | `WF` | Watchdog flag; write zero to clear |
| 0 | `WD_EN` | Watchdog enable |

## 15. `Interrupt Status` - Read `FEh`

Read-only. Bits are self-clearing when this register is read.

| Bit | Name | Meaning |
|---:|---|---|
| 15 | `TO` | Timeout |
| 14 | `AF` | Alarm flag |
| 13 | `X` | Reserved |
| 12 | `TOF` | `TOF_UP`, `TOF_DN`, or `TOF_DIFF` completed |
| 11 | `TE` | Temperature command completed |
| 10 | `LDO` | Internal LDO stabilized |
| 9 | `TOF_EVTMG` | Event timing TOF sequence completed |
| 8 | `TEMP_EVTMG` | Event timing temperature sequence completed |
| 7 | `FLASH` | Flash ready / flash operation completed |
| 6 | `CAL` | Manual calibrate command completed |
| 5 | `HALT` | HALT completed |
| 4 | `CSWI` | Case switch/tamper detected |
| 3 | `INIT` | Initialize completed |
| 2 | `POR` | Power-on reset |
| 1:0 | `X` | Reserved |

Firmware pattern:

```text
INT asserted
→ read FEh
→ decode flags
→ read result registers associated with those flags
```

Reading `FEh` clears all currently asserted status bits. Therefore:

- Only one driver component should own the physical status read.
- Cache the 16-bit status snapshot and dispatch software events from that cached value.
- Do not let separate TOF, temperature, flash, and calibration services each read `FEh` independently.
- A newly arriving interrupt source during the read is asserted after the read, so the driver must re-check the active-low `INT` line or process another edge/level event.
- `INT_EN` controls the external pin; status bookkeeping still needs to be handled correctly when polling.

## 16. `Control` Arm Flags and Opcode Ambiguity

The Control register contains:

| Bit | Name | Behavior |
|---:|---|---|
| 9 | `AFA` | Set with RTC alarm; must be written `0` after updating alarm settings to re-arm |
| 8 | `CSWA` | Set with case-switch event; must be written `0` to re-arm tamper detection |

Rev 2 contains conflicting presentation:

1. The register memory map shows Control as read-only with read opcode `FFh`.
2. The detailed description says `AFA` and `CSWA` must be written to zero.
3. Table 23 displays `FFh` under “WRITE OPCODE” and `7Fh` under “READ OPCODE”, which conflicts with the general read range `B0h`-`FFh`, the memory map, and the semantic need to read at `FFh`.

The likely intended special clear-write opcode is `7Fh` with read at `FFh`, but this is an inference, not a confirmed statement in the supplied datasheet. Keep this feature behind a dedicated function and require one of the following before production use:

- official Analog Devices clarification or erratum;
- a known-good reference driver;
- controlled hardware verification that proves the transaction and does not alter unrelated state.

Do not expose a generic `write_register(0xFF, value)` workaround.

## 17. Important Datasheet Consistency Notes

These points should be handled carefully in firmware:

1. The detailed `TOF2` table marks bit 3 as reserved. Some memory-map OCR/table layouts can make it appear like `EN_UP_DN`; for firmware, prefer the detailed register table and write bit 3 as `0`.
2. In the "Average ICC vs. ToF Rate Configuration Settings" table, the 3-hit example lists `STOP[2:0] = 101`, but the detailed `TOF2` table defines 3 hits as `010` and `101` as 6 hits. Treat the detailed `TOF2` register table as authoritative unless an errata/app note says otherwise.
3. Table 23's Control read/write opcode headings conflict with the memory map and the general opcode ranges. Do not silently choose an opcode; follow Section 16.
4. The detailed `CAL_PERIOD` table says cycles equal `1 + CAL_PERIOD`, while a calibration prose example describes field value 6 as six measurements. Verify the selected semantics.
5. Some event flowchart text says `MAX35101`, indicating inherited artwork. Use the MAX35103 command descriptions and detailed register tables for implementation.
6. Event cycle count registers report valid error-free cycles, not simply the requested `1 + TDM` or `1 + TMM`; code must not assume equality.
