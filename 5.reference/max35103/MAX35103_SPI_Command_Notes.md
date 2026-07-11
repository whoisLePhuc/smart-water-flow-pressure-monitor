---
title: MAX35103 SPI Command Notes
source_pdf: max35103.pdf, Rev 2, February 2026
purpose: PDF-checked SPI, command, register, and flash access notes
status: Reviewed against the official Rev 2 PDF datasheet
device: MAX35103
---

# MAX35103 SPI Command Notes

> **Source of truth:** [MAX35103 Rev 2 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max35103.pdf), Figures 1, 2, and 13-20 plus Tables 2, 3, 22, 23, and 25.

## 1. SPI Interface Basics

MAX35103 uses an opcode-based SPI-compatible serial interface.

| Pin | Direction from MCU | Meaning |
|---|---|---|
| `CE` | Output | Chip enable. Active low. Starts and terminates transfer. |
| `SCK` | Output | Serial clock from MCU. Active only while `CE` is low. |
| `DIN` | Output | Data from MCU to MAX35103. |
| `DOUT` | Input | Data from MAX35103 to MCU. |
| `INT` | Input / interrupt | Active-low interrupt output from MAX35103. |

Protocol notes:

- Opcodes are 8 bits.
- Register words are 16 bits.
- Opcodes and data are transferred MSB first.
- `DOUT` is high impedance when the device is not driving read data.
- Read and write operations can auto-increment for consecutive registers.
- SCK is idle low and DIN is latched on the falling edge. Configure the MCU as `CPOL=0`, `CPHA=1` (SPI mode 1).
- Maximum SCK is 20 MHz at VCC >= 3.0 V and 10 MHz at VCC = 2.3 V. Start bring-up at a much lower rate, then increase only after timing margins are verified.
- `INT` is active-low open-drain and requires an external pull-up. Configure the MCU interrupt as falling-edge or active-low level according to the power/wakeup design.
- Respect CE setup/hold/high-time and data timing from the electrical table, not only the logical diagrams.
- Keep CE asserted for the complete opcode plus data/address phase. Deasserting CE completes an execution command and terminates register/flash streaming.
- Validate mode, byte order, CE boundaries, and DOUT high-impedance behavior with a logic analyzer.

### 1.1 STM32 configuration baseline

```c
// Conceptual STM32 HAL configuration
hspi.Init.CLKPolarity = SPI_POLARITY_LOW;
hspi.Init.CLKPhase    = SPI_PHASE_2EDGE;
hspi.Init.FirstBit    = SPI_FIRSTBIT_MSB;
hspi.Init.DataSize    = SPI_DATASIZE_8BIT;
hspi.Init.NSS         = SPI_NSS_SOFT;   // CE is controlled as a GPIO.
```

Use 8-bit SPI frames even though registers are 16 bits. This keeps opcode, address, and data framing explicit and portable across MCU SPI peripherals.

## 2. Opcode Classes

| Class | Opcode(s) | Purpose |
|---|---:|---|
| Execution commands | `00h` to `0Eh` | Start internal actions such as TOF, temperature, initialize, event timing, calibration |
| Register read | `B0h` to `FFh` | Read one or more 16-bit registers |
| Register write | `30h` to `43h` | Write one or more 16-bit writable registers |
| Flash read | `90h` | Read user flash |
| Flash write | `10h` | Write user flash |
| Flash block erase | `13h` | Erase flash block |

The Control arm flags are a documented exception/ambiguity. The memory map reads Control at `FFh`, while Table 23 presents conflicting read/write labels and the text requires write-zero re-arm behavior. Do not treat `7Fh` or `FFh` as an ordinary register write without resolving the issue described in `MAX35103_Register_Notes.md`.

## 3. Execution Opcode Commands

Execution commands are single-byte commands. The MAX35103 starts executing the command after it receives all 8 bits and `CE` is deasserted.

| Command | Opcode | Firmware use |
|---|---:|---|
| `TOF_UP` | `00h` | Single upstream TOF measurement |
| `TOF_DOWN` | `01h` | Single downstream TOF measurement |
| `TOF_DIFF` | `02h` | Back-to-back upstream/downstream measurement and differential calculation |
| `Temperature` | `03h` | RTD/temperature timing measurement |
| `Reset` | `04h` | Perform the essential POR behavior: recall programmed configuration and clear result/status state |
| `Initialize` | `05h` | Recall configuration from configuration flash and start the TDC so TOF/temperature commands can run |
| `Transfer configuration to flash` | `06h` | Store configuration registers in nonvolatile configuration flash |
| `EVTMG1` | `07h` | Automatic `TOF_DIFF` plus temperature sequence |
| `EVTMG2` | `08h` | Automatic `TOF_DIFF` sequence |
| `EVTMG3` | `09h` | Automatic temperature sequence |
| `HALT` | `0Ah` | Stop event timing after current internal measurement completes |
| `LDO_Timed` | `0Bh` | Enable flash LDO for short flash access |
| `LDO_ON` | `0Ch` | Enable flash LDO for multiple flash operations |
| `LDO_OFF` | `0Dh` | Disable flash LDO |
| `Calibrate` | `0Eh` | Run TDC calibration routine |

### 3.1 Execution command transaction

```text
CE   : low  __________ high
DIN  : [opcode: 8 bits]
DOUT : high impedance
```

Example C-like pseudo-code:

```c
bool max35103_send_command(max35103_t *dev, uint8_t opcode)
{
    max35103_ce_low(dev);
    bool ok = spi_write_byte(dev->spi, opcode);
    max35103_ce_high(dev);
    return ok;
}
```

Do not send another command merely because the opcode byte was transmitted successfully. Command completion is a separate device event reported through status/`INT`. `HALT`, flash-related commands, and `Initialize` can take significant time.

## 4. Register Read

Read opcodes are `B0h` through `FFh`.

```text
CE   : low  __________________________________ high
DIN  : [read opcode: 8 bits] [dummy clocks...]
DOUT : Z                     [data: 16 bits]
```

Pseudo-code:

```c
bool max35103_read_reg16(max35103_t *dev, uint8_t read_opcode, uint16_t *value)
{
    uint8_t tx[3] = { read_opcode, 0x00, 0x00 };
    uint8_t rx[3] = { 0 };

    max35103_ce_low(dev);
    bool ok = spi_transfer(dev->spi, tx, rx, sizeof(tx));
    max35103_ce_high(dev);

    if (!ok) {
        return false;
    }

    *value = ((uint16_t)rx[1] << 8) | rx[2];
    return true;
}
```

## 5. Continuous Register Read

The read command can continue across consecutive registers while `CE` remains asserted. The address counter auto-increments.

This is useful for result blocks:

- `AVGUPInt`, `AVGUPFrac`
- `AVGDNInt`, `AVGDNFrac`
- `TOF_DIFFInt`, `TOF_DIFFFrac`
- `TOF_DIFF_AVGInt`, `TOF_DIFF_AVGFrac`
- temperature result groups
- calibration result pair

Pseudo-code:

```c
bool max35103_read_regs16(max35103_t *dev,
                          uint8_t start_read_opcode,
                          uint16_t *values,
                          size_t count)
{
    max35103_ce_low(dev);

    if (!spi_write_byte(dev->spi, start_read_opcode)) {
        max35103_ce_high(dev);
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        uint8_t rx[2] = {0};
        uint8_t tx[2] = {0x00, 0x00};

        if (!spi_transfer(dev->spi, tx, rx, sizeof(rx))) {
            max35103_ce_high(dev);
            return false;
        }

        values[i] = ((uint16_t)rx[0] << 8) | rx[1];
    }

    max35103_ce_high(dev);
    return true;
}
```

Continuous-read safety rules:

- Keep the requested range inside a known result/configuration block.
- Do not stream unintentionally into `FEh`: reading Interrupt Status clears all asserted flags.
- Do not read beyond `FFh`; the internal counter behavior across the boundary is not a driver API contract.
- Return the start opcode and word count in diagnostics when a transfer fails.

## 6. Register Write

Write opcodes are `30h` through `43h`.

```text
CE   : low  __________________________________ high
DIN  : [write opcode: 8 bits] [data: 16 bits]
DOUT : high impedance
```

Pseudo-code:

```c
bool max35103_write_reg16(max35103_t *dev, uint8_t write_opcode, uint16_t value)
{
    uint8_t tx[3] = {
        write_opcode,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF),
    };

    max35103_ce_low(dev);
    bool ok = spi_write(dev->spi, tx, sizeof(tx));
    max35103_ce_high(dev);

    return ok;
}
```

## 7. Continuous Register Write

The write command can write consecutive writable registers while `CE` stays low. The address counter auto-increments after each 16-bit word.

This is useful for writing configuration blocks such as:

```text
TOF1 -> TOF7
Event Timing 1 -> Event Timing 2
TOF Measurement Delay
Calibration and Control
Real-Time Clock
```

Pseudo-code:

```c
bool max35103_write_regs16(max35103_t *dev,
                           uint8_t start_write_opcode,
                           const uint16_t *values,
                           size_t count)
{
    max35103_ce_low(dev);

    if (!spi_write_byte(dev->spi, start_write_opcode)) {
        max35103_ce_high(dev);
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        uint8_t tx[2] = {
            (uint8_t)(values[i] >> 8),
            (uint8_t)(values[i] & 0xFF),
        };

        if (!spi_write(dev->spi, tx, sizeof(tx))) {
            max35103_ce_high(dev);
            return false;
        }
    }

    max35103_ce_high(dev);
    return true;
}
```

Before a multi-register write, construct all words with reserved bits cleared. After the write, read the block back and compare only documented writable bits. Do not rely on a generic `read_opcode = write_opcode | 0x80` transformation for special/status/control operations; use named constants.

## 8. Recommended Bring-Up Sequence

```text
Power-on
→ wait for POR
→ read Interrupt Status (FEh)
→ choose stored configuration or stage a new one
→ if new: write registers and Transfer Configuration to Flash (06h)
→ wait until transfer completes and SPI is available
→ Initialize (05h)
→ wait until initialization completes and SPI is available
→ Calibrate (0Eh)
→ run TOF_UP / TOF_DOWN / TOF_DIFF
→ read status and results
```

`Initialize` recalls configuration from flash. Therefore the sequence “write volatile configuration → skip opcode `06h` → Initialize” is incorrect because the unsaved values can be replaced by the older flash configuration.

Normal production boot should not rewrite configuration flash every time. Store the validated configuration during manufacturing or an intentional configuration update, then boot by recalling/initializing that stored image.

Recommended firmware workflow:

```c
bool max35103_basic_init(max35103_t *dev)
{
    uint16_t status;

    if (!max35103_read_reg16(dev, 0xFE, &status)) {
        return false;
    }
    if ((status & MAX35103_INT_POR) == 0u) {
        // Not necessarily fatal after a warm reset, but record the observation.
    }

    // At this point either use the already stored configuration or run a
    // separate, deliberate store_configuration() workflow before Initialize.

    if (!max35103_send_command(dev, MAX35103_CMD_INITIALIZE)) {
        return false;
    }
    if (!max35103_wait_command_complete(dev, MAX35103_INT_INIT,
                                        MAX35103_INIT_TIMEOUT_MS)) {
        return false;
    }

    if (!max35103_send_command(dev, MAX35103_CMD_CALIBRATE)) {
        return false;
    }
    if (!max35103_wait_command_complete(dev, MAX35103_INT_CAL,
                                        MAX35103_CAL_TIMEOUT_MS)) {
        return false;
    }

    return true;
}
```

`max35103_wait_command_complete()` must support both interrupt-driven and bounded polling operation. This matters when the stored configuration has `INT_EN=0`. Never poll the SPI bus while the datasheet says it is unavailable during a flash/initialize operation; the polling fallback begins only after the documented maximum busy interval or another hardware-ready indication defined by the integration.

## 9. `TOF_DIFF` Measurement Workflow

```text
Configure TOF1/TOF2/TOF3/TOF4/TOF5/TOF6/TOF7
→ configure TOF Measurement Delay
→ enable INT_EN
→ send TOF_DIFF opcode 02h
→ wait for INT
→ read Interrupt Status FEh
→ check TOF and TO
→ read AVGUP, AVGDN, TOF_DIFF
→ reject documented error sentinels
→ convert fixed-point result
→ apply zero-flow offset and calibration algorithm
```

Important result registers:

| Register | Read opcode |
|---|---:|
| `AVGUPInt` | `D1h` |
| `AVGUPFrac` | `D2h` |
| `AVGDNInt` | `E0h` |
| `AVGDNFrac` | `E1h` |
| `TOF_DIFFInt` | `E2h` |
| `TOF_DIFFFrac` | `E3h` |

For event mode, also read `E4h`-`E6h` in one burst:

- `E4h[15:8]`: `TOF_Range`.
- `E4h[7:0]`: valid cycle count.
- `E5h:E6h`: signed `TOF_DIFF_AVG`.

Reject the average if the count is zero. Do not assume it equals the requested `1 + TDM`; failed cycles are excluded.

## 10. Event Timing Workflow

For low-power water metering, event timing is usually better than manually triggering every measurement.

### EVTMG2 - automatic TOF_DIFF

```text
Configure TOF registers
→ configure Event Timing 1: TDF, TDM, optional 8XS
→ configure Calibration and Control: INT_EN, ET_CONT, CONT_INT
→ send EVTMG2 opcode 08h
→ MCU sleeps
→ INT wakes MCU
→ read FEh
→ if TOF_EVTMG: read TOF_DIFF_AVG and diagnostics
```

### EVTMG1 - automatic TOF_DIFF + temperature

```text
Configure TOF registers
→ configure temperature port sequence
→ configure Event Timing 1 and 2
→ configure calibration policy
→ send EVTMG1 opcode 07h
→ MCU sleeps
→ INT wakes MCU
→ read FEh
→ read TOF and/or temperature result blocks according to flags
```

When `CONT_INT=1`, the device can interrupt after each individual cycle. When it is zero, process the sequence-complete flags. Event-mode errors do not automatically terminate the sequence; use status, sentinels, cycle counts, and range evidence to decide sample quality.

To stop continuous event timing:

```text
send HALT (0Ah)
→ do not assume an immediate stop
→ device completes the active TOF or temperature command
→ wait for HALT status
→ read the now-frozen result snapshot
```

## 11. Flash Access Notes

Flash access uses the internal LDO.

| Command | Opcode | Use |
|---|---:|---|
| `LDO_Timed` | `0Bh` | Short flash access burst; LDO auto behavior reduces command overhead |
| `LDO_ON` | `0Ch` | Multiple flash accesses where CE toggles between words |
| `LDO_OFF` | `0Dh` | Turn off flash LDO after `LDO_ON` |
| `Read FLASH` | `90h` | Read user flash |
| `Write FLASH` | `10h` | Write user flash |
| `Block Erase FLASH` | `13h` | Erase flash block |

Firmware pattern:

```text
LDO_ON or LDO_Timed
→ wait for INT
→ read FEh and check LDO
→ perform flash command
→ wait for completion if required
→ if LDO_ON was used, send LDO_OFF
```

### 11.1 User flash transaction formats

Read, write, and erase use a 16-bit byte address. Valid starting addresses are even and range from `0000h` through `1FFFh`.

```text
Read:  CE low → 90h → address[15:0] → clock continuous data → CE high
Write: CE low → 10h → address[15:0] → one data word[15:0] → CE high
Erase: CE low → 13h → address[15:0] → CE high
```

Rules:

- User flash is 8 KB, organized as 16-bit words; erased words read `FFFFh`.
- Sequential read auto-increments and wraps after the final location.
- A write location must already be erased.
- One write command programs one 16-bit word. If more complete data words are clocked during the same CE assertion, the device writes only the last bounded word; it is not a FIFO or multiword program command.
- Block erase size is 128 words = 256 bytes; there are 32 blocks.
- The block containing the supplied address is erased, so align/check the address in firmware before issuing erase.
- Wait for `FLASH` completion or the documented erase time before the next operation.
- Configuration flash used by opcode `06h` is separate from this 8 KB array.

Recommended user-flash write policy:

```text
validate address and block ownership
→ preserve still-needed words from the target block
→ erase block
→ rewrite required words one at a time
→ read back and verify
→ only then update higher-level metadata/commit marker
```

The MAX35103 flash is not automatically an atomic filesystem. Product firmware must add versioning, CRC, commit state, and power-loss recovery if it stores critical records there.

## 12. Interrupt Handling Pattern

`INT` is active-low and requires `INT_EN = 1` in the Calibration and Control register.

```text
INT falling edge
→ MCU wakes
→ read Interrupt Status FEh
→ flags clear on read
→ handle results according to bits
```

Suggested handler:

```c
void max35103_irq_task(max35103_t *dev)
{
    uint16_t isr = 0;
    if (!max35103_read_reg16(dev, 0xFE, &isr)) {
        return;
    }

    if (isr & (1u << 15)) {
        // TO timeout. Mark current measurement invalid.
    }

    if (isr & (1u << 12)) {
        // Single TOF command completed.
    }

    if (isr & (1u << 9)) {
        // Event timing TOF sequence completed.
    }

    if (isr & (1u << 8)) {
        // Event timing temperature sequence completed.
    }

    if (isr & (1u << 6)) {
        // Manual calibration completed.
    }
}
```

Because `FEh` is self-clearing, this function must be the single owner of status reads. It should publish a cached software event; downstream modules must not read `FEh` again. After processing, check whether `INT` remains low because an event can arrive during the status read.

Avoid doing long SPI transfers inside the MCU EXTI ISR. The ISR should timestamp/notify a driver task, and the task should read and dispatch status.

## 13. Command State and Failure Model

Suggested driver command states:

```text
IDLE → TX_OPCODE → DEVICE_BUSY → STATUS_READY → READ_RESULTS → IDLE
                         └── timeout/SPI error → RECOVERY
```

Each operation should define:

- expected completion bit(s);
- whether SPI is available while the command runs;
- maximum firmware timeout;
- result block to read;
- error sentinels to reject;
- retry safety;
- recovery action.

Do not blindly retry commands with side effects such as flash write, block erase, or configuration transfer. First determine whether the previous operation completed by reading status and verifying memory.

## 14. Practical SPI Debug Checklist

- Verify `CE` is active-low and returns high at the end of every command.
- Verify opcode is MSB first.
- Verify register data is MSB first.
- Verify SCK idles low and the MAX35103 sees DIN on falling edges (mode 1).
- Check that `DOUT` is high impedance except during read data.
- Read `FEh` after power-on and confirm `POR`.
- Write a non-critical RTC register and read it back.
- Write `TOF1`, read `TOF1`, compare value.
- Send `Initialize`, then check `INIT`.
- Send `Calibrate`, then check `CAL` and read `F8h/F9h`.
- Verify that `Initialize` restores the configuration stored in flash rather than unsaved volatile edits.
- Verify `FEh` is read only once per interrupt and that the cached flags reach every consumer.
- Verify invalid TOF, open/short temperature, and zero-cycle event-average handling.
- Verify HALT waits for the active internal measurement to complete.
- Verify user-flash even-address checks, single-word writes, block boundaries, and readback.

## 15. Minimum Driver Tests

| Test | Expected result |
|---|---|
| Mode/bit order | Captured opcode is MSB first with idle-low SCK and falling-edge sampling |
| Read `FEh` after POR | POR observation is captured and the self-clearing behavior is handled |
| Register write/readback | Documented bits match; reserved bits remain zero |
| Configuration recall | Stored image is restored by `Initialize` |
| Unsaved configuration test | Confirms why volatile edits cannot be assumed to survive `Initialize` |
| Positive/negative `TOF_DIFF` vectors | Signed Q16 conversion matches datasheet examples |
| TOF timeout | `TO` is reported and invalid result sentinels are rejected |
| Event errors | Valid cycle count can be lower than requested without corrupting the average |
| Status race | A new event arriving during `FEh` read is processed on the next low/edge condition |
| Flash write/verify | Only the intended even-address word changes |
| Power interruption around flash | Higher-level commit/CRC scheme detects incomplete update |
| HALT | Results become stable only after HALT completion status |
