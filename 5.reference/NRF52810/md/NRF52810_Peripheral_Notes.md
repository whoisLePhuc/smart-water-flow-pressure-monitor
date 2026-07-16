---
title: nRF52810 Peripheral Notes
source_specification: nRF52810 Product Specification v1.2
purpose: Firmware-oriented notes for tasks, events, interrupts, PPI, GPIO, timers, serial interfaces, analog, waveform, and system peripherals
status: Reviewed against the official Nordic Product Specification and Revision 3 Errata
device: nRF52810
---

# nRF52810 Peripheral Notes

> **Source of truth:** [nRF52810 Product Specification](https://docs.nordicsemi.com/r/bundle/ps_nrf52810/), version 1.2, and the errata document for the actual silicon revision. This document summarizes implementation patterns, resource constraints, and safe workflows; it does not replace peripheral register tables, electrical timing, or revision-specific anomaly descriptions.

Related notes:

- [`NRF52810_Technical_Summary.md`](NRF52810_Technical_Summary.md): CPU, memory, EasyDMA, shared peripherals, clocks, power, and radio overview.
- [`NRF52810_Memory_Register_Notes.md`](NRF52810_Memory_Register_Notes.md): memory map, shared base addresses, NVMC, FICR, UICR, BPROT, and register-access rules.
- [`NRF52810_Clock_Power_Notes.md`](NRF52810_Clock_Power_Notes.md): peripheral clock dependencies, low-power behavior, and RAM retention.
- [`NRF52810_Radio_BLE_Notes.md`](NRF52810_Radio_BLE_Notes.md): RADIO state machine, BLE timing, packet buffers, and RF constraints.
- [`NRF52810_Firmware_Platform_Design.md`](NRF52810_Firmware_Platform_Design.md): driver ownership, interrupt dispatch, error handling, and platform APIs.

## 1. Peripheral Programming Model

nRF52810 peripherals use a common task/event model:

```text
firmware configures registers and pins
→ firmware or PPI triggers a TASK
→ peripheral performs the operation
→ peripheral raises one or more EVENTS
→ event can trigger an interrupt, shortcut, or PPI channel
→ firmware reads status/AMOUNT/error registers
→ firmware clears events and releases resources
```

| Mechanism | Meaning |
|---|---|
| Task | Write `1` to trigger a hardware action. |
| Event | Hardware writes `1` when a condition occurs; firmware clears it by writing `0`. |
| Shortcut | Fixed event-to-task connection inside one peripheral. |
| PPI | Programmable event-to-task connection across peripherals. |
| Interrupt | One NVIC interrupt per peripheral ID; multiple events may share it. |
| EasyDMA | Peripheral reads or writes Data RAM without CPU byte transfers. |

Most peripherals should be configured in this order:

1. disable the peripheral;
2. configure GPIO output idle levels;
3. configure `PSEL` registers;
4. clear stale events and errors;
5. configure DMA pointers, limits, mode, and frequency;
6. configure shortcuts/PPI;
7. configure interrupt enables;
8. enable the peripheral;
9. trigger the first task.

Do not rely on reset values inherited from another peripheral that shares the same ID.

## 2. Events and Interrupt Handling

An event can be generated even when its event register is already `1`. Leaving an event uncleared can therefore hide the boundary between two operations.

Recommended interrupt-handler pattern:

```c
void PERIPHERAL_IRQHandler(void)
{
    if (PERIPHERAL->EVENTS_END != 0u) {
        PERIPHERAL->EVENTS_END = 0u;
        (void)PERIPHERAL->EVENTS_END;
        handle_end();
    }

    if (PERIPHERAL->EVENTS_ERROR != 0u) {
        PERIPHERAL->EVENTS_ERROR = 0u;
        (void)PERIPHERAL->EVENTS_ERROR;
        handle_error();
    }
}
```

Clearing an event or disabling an interrupt can take up to four CPU clock cycles to affect the interrupt line. A volatile read-back of the event or `INTENCLR` register before leaving the ISR prevents an immediate false re-entry.

Other rules:

- inspect every enabled event source in a shared ISR;
- clear the event before delivering the software callback;
- capture `ERRORSRC`, `STATUS`, and `AMOUNT` before clearing them;
- keep ISR work bounded;
- do not call a blocking driver API from the ISR unless the API explicitly supports it;
- assign interrupt priorities through one platform policy compatible with the radio stack.

## 3. Shared Peripheral IDs

Peripherals at the same ID/base address are mutually exclusive except for the special ID 0 block.

| ID / base | Alternate instances |
|---|---|
| `2 / 0x40002000` | `UART0`, `UARTE0` |
| `3 / 0x40003000` | `TWI0`, `TWIM0`, `TWIS0` |
| `4 / 0x40004000` | `SPI0`, `SPIM0`, `SPIS0` |
| `15 / 0x4000F000` | `CCM`, `AAR` |
| `20 / 0x40014000` | `EGU0`, `SWI0` |
| `21 / 0x40015000` | `EGU1`, `SWI1` |

Safe switching sequence:

```text
stop current transaction
→ wait for STOPPED / RXTO / TXSTOPPED as applicable
→ disable current peripheral
→ remove PPI connections
→ INTENCLR = 0xFFFFFFFF
→ clear events and errors
→ configure the new peripheral from scratch
→ enable the new peripheral
```

The legacy `UART`, `TWI`, and `SPI` implementations are marked deprecated. New designs should normally use UARTE, TWIM/TWIS, and SPIM/SPIS.

## 4. Peripheral Resource Summary

| Peripheral | Instances / channels | Clock / DMA note |
|---|---|---|
| GPIO | One P0 port, up to 32 pins | Pin count depends on package. |
| GPIOTE | 8 channels plus PORT event | Tasks/events can connect through PPI. |
| PPI | 20 configurable + 12 fixed channels | Six channel groups; one fork task endpoint per channel. |
| TIMER | TIMER0–2, four CC registers each | Uses high-frequency clock domain. |
| RTC | RTC0 with 3 CC; RTC1 with 4 CC | 24-bit counter from LFCLK. |
| WDT | One instance | Runs from LFCLK. |
| UARTE | UARTE0 | Full duplex, EasyDMA, up to 1 Mbps. |
| TWIM/TWIS | One shared instance | EasyDMA; TWIM supports 100/250/400 kbps. |
| SPIM/SPIS | One shared instance | EasyDMA; SPIM supports up to 8 Mbps. |
| SAADC | 8 configurable channels | EasyDMA result buffer. |
| COMP | One comparator | Analog threshold/window detection. |
| TEMP | One die-temperature peripheral | Uses factory coefficients from FICR. |
| PWM | PWM0, four output channels | EasyDMA sequence playback. |
| PDM | One input interface | EasyDMA audio sample buffers. |
| QDEC | One instance | Hardware quadrature accumulation. |
| RNG | One instance | Random byte event stream. |
| ECB | One instance | AES-128 block operation. |
| CCM/AAR | Shared ID 15 | BLE security/address-resolution hardware. |

## 5. GPIO

GPIO P0 is located at `0x50000000`. Each bonded pin has a `PIN_CNF[n]` register controlling:

- input connect/disconnect;
- direction;
- pull-up/pull-down;
- drive strength;
- high/low sense level.

Use `OUTSET`, `OUTCLR`, `DIRSET`, and `DIRCLR` for atomic bit changes instead of read-modify-write on the whole port.

Important registers:

| Register | Purpose |
|---|---|
| `OUT` | Output latch value |
| `OUTSET` / `OUTCLR` | Atomic output set/clear |
| `IN` | Pin input state; valid only when input buffer is connected |
| `DIRSET` / `DIRCLR` | Atomic direction control |
| `LATCH` | Pins that have met their `SENSE` condition |
| `DETECTMODE` | Direct DETECT or latched DETECT behavior |
| `PIN_CNF[n]` | Per-pin direction, pull, drive, input, and sense configuration |

### 5.1 Low-power GPIO rules

- Disconnect the input buffer when a pin is not used as an input.
- Do not leave digital inputs floating.
- Avoid keeping an input voltage between `VIL` and `VIH`; it can increase current.
- Match unused-pin state to the external circuit to avoid leakage through protection paths.
- Configure output value before changing the pin to output to avoid glitches.
- Reconfigure peripheral-driven pins to safe GPIO states before System OFF.

### 5.2 GPIO SENSE and wake-up

`SENSE` contributes to the GPIO `DETECT` signal and can wake the device from System OFF.

The pin must be at a non-triggering level before SENSE is enabled. Otherwise DETECT becomes active immediately and the device may wake again as soon as it enters System OFF.

The `LATCH` bit is cleared by writing `1`, but it cannot clear while the corresponding SENSE condition is still active.

Recommended wake handling:

```text
wake reset
→ read RESETREAS
→ read GPIO.LATCH
→ remove or reverse the active SENSE condition
→ clear LATCH
→ verify DETECT is inactive
→ process the wake source
```

## 6. GPIOTE

GPIOTE provides eight channels. Each channel can be configured as:

- an input event from a selected GPIO; or
- an output task acting on a selected GPIO.

Per-channel resources include:

| Resource | Function |
|---|---|
| `EVENTS_IN[n]` | Edge/event from the configured input pin |
| `TASKS_OUT[n]` | Set, clear, or toggle according to polarity |
| `TASKS_SET[n]` | Drive the configured output high |
| `TASKS_CLR[n]` | Drive the configured output low |
| `CONFIG[n]` | Mode, pin, polarity, and initial output state |

`EVENTS_PORT` is shared by GPIO pins using the SENSE mechanism and does not consume a dedicated GPIOTE channel per pin.

Use dedicated channels when exact edge timing or PPI routing is required. Use the PORT/SENSE mechanism when many low-rate pins need wake or interrupt detection and channel conservation is more important than edge timestamp precision.

Typical capture path:

```text
GPIO edge
→ GPIOTE EVENTS_IN[n]
→ PPI
→ TIMER TASKS_CAPTURE[m]
→ timestamp stored in TIMER CC[m]
```

## 7. PPI

PPI contains:

- channels `0–19`: fully configurable event/task endpoints;
- channels `20–31`: fixed event/task endpoints;
- six channel groups;
- a fork task endpoint for each channel.

A configurable channel contains:

```text
EEP  = address of event register
TEP  = address of primary task register
FORK = optional second task register
```

Shortcuts are faster than equivalent PPI routes because they are not subject to the PPI 16 MHz synchronization delay.

Important fixed channels include RADIO timing links with TIMER0, RTC0, CCM, and AAR. A BLE stack may own fixed or configurable PPI resources. Application firmware must obtain resources through the selected stack/SDK allocation mechanism.

PPI configuration workflow:

1. allocate a channel and optional group;
2. disable the channel;
3. clear stale source events;
4. write EEP, TEP, and optional FORK;
5. configure both source and destination peripherals;
6. enable the channel only when both endpoints are ready;
7. disable and disconnect it before either peripheral is released.

Do not leave a PPI route active while changing an endpoint peripheral's mode or shared ID.

## 8. TIMER

nRF52810 has TIMER0, TIMER1, and TIMER2. Each instance implements four capture/compare registers `CC[0..3]`.

TIMER can operate in:

- timer mode, incremented from the high-frequency clock; or
- counter mode, incremented by `TASKS_COUNT`.

The timer base frequency is:

```text
f_TIMER = 16 MHz / 2^PRESCALER
```

`BITMODE` selects the active counter width. Firmware must handle wraparound according to the selected width.

Typical periodic event:

```text
configure MODE, BITMODE, PRESCALER
→ CC[0] = period counts
→ enable COMPARE0_CLEAR shortcut
→ clear EVENTS_COMPARE[0]
→ START
```

Typical input timestamp:

```text
GPIOTE event
→ PPI
→ TIMER CAPTURE task
→ read CC[n]
```

TIMER keeps high-frequency resources active. Stop unused timers instead of leaving them running as general timebases.

If START and STOP are triggered in the same PCLK16M period, STOP has priority.

## 9. RTC

RTC0 and RTC1 are low-power counters driven by LFCLK.

| Instance | Compare channels |
|---|---|
| RTC0 | `CC[0..2]` |
| RTC1 | `CC[0..3]` |

The counter is 24 bit. At `PRESCALER = 0`:

```text
tick period = 1 / 32768 Hz ≈ 30.517 µs
overflow    = 2^24 / 32768 ≈ 512 s
```

General frequency:

```text
f_RTC = 32768 / (PRESCALER + 1)
```

`PRESCALER` must be written while the RTC is stopped. Writing it after START has no effect.

The TICK and OVRFLW events are disabled by default. Enable only required event routing through `EVTEN`; unnecessary high-rate TICK events can increase power.

RTC compare values must account for the 24-bit wrap:

```c
#define RTC_MASK 0x00FFFFFFu

uint32_t rtc_add(uint32_t now, uint32_t delta)
{
    return (now + delta) & RTC_MASK;
}
```

Use RTC rather than SysTick for low-power scheduling when the CPU is expected to sleep.

## 10. WDT

WDT is a countdown watchdog driven by LFCLK.

Key behavior:

- START begins watchdog operation.
- `CRV` defines the countdown reload value.
- enabled reload-request registers provide independent health channels.
- WDT can be configured to run or pause during CPU sleep and debug halt.
- timeout generates the watchdog event/reset behavior defined by the peripheral.

A reload channel should represent a real health condition:

```text
communication healthy
AND sensor service healthy
AND storage not deadlocked
AND main event loop progressing
→ permit watchdog reload
```

Do not feed WDT unconditionally from a periodic interrupt. That pattern continues to feed even if the application event loop is deadlocked.

Record watchdog reset reason and a small retained fault context early after reboot.

## 11. UARTE

UARTE0 supports full-duplex communication, CTS/RTS hardware flow control, optional even parity, one or two stop bits, EasyDMA, and baud rates up to 1 Mbps.

Stable high-speed communication requires the external high-accuracy clock source specified by Nordic.

### 11.1 TX sequence

```text
prepare RAM buffer
→ TXD.PTR / TXD.MAXCNT
→ clear ENDTX and TXSTOPPED
→ STARTTX
→ wait ENDTX
→ STOPTX if the transmitter must be stopped
→ wait TXSTOPPED
```

### 11.2 RX sequence

```text
prepare RAM buffer
→ RXD.PTR / RXD.MAXCNT
→ clear ENDRX, RXTO, ERROR
→ STARTRX
→ process ENDRX and RXD.AMOUNT
→ STOPRX
→ wait RXTO
→ FLUSHRX if FIFO content must be moved to RAM
```

`RXDRDY` can occur before the corresponding byte has reached Data RAM. Use `ENDRX`/`RXTO` and `RXD.AMOUNT` for buffer ownership and byte count.

After `STOPRX`, up to four additional bytes can be received. With flow control disabled, overflow can occur without an RTS warning to the sender.

Before disabling UARTE, wait for `TXSTOPPED` and `RXTO` after issued stop tasks.

## 12. TWIM and TWIS

TWIM0 and TWIS0 share peripheral ID 3 and cannot operate simultaneously.

TWIM features:

- I²C-compatible single-master operation;
- 100, 250, and 400 kbps;
- EasyDMA TX/RX;
- repeated-start support;
- slave clock stretching support, with a documented timing caveat after stretching.

TWIM does not stop automatically when:

- the DMA buffer finishes; or
- an ERROR/NACK occurs.

The STOP task must be issued by shortcut, PPI, or software. Recommended shortcuts include `LASTTX→STOP` and `LASTRX→STOP` for simple transactions.

Combined register read:

```text
STARTTX register address
→ LASTTX triggers STARTRX through shortcut
→ receive RXD.MAXCNT bytes
→ LASTRX triggers STOP
→ STOPPED confirms EasyDMA no longer owns buffers
```

Error handling:

1. capture `ERRORSRC`;
2. clear the error bits using the documented write-one-to-clear behavior;
3. resume if the peripheral is suspended;
4. issue STOP;
5. wait for STOPPED;
6. verify line levels;
7. perform project-level bus recovery if SDA/SCL remain stuck.

TWIS operation is driven by the external master. Driver design must prepare buffers before granting the transaction and handle read/write request, stopped, and error events without assuming master timing.

## 13. SPIM and SPIS

SPIM0 and SPIS0 share peripheral ID 4.

SPIM supports:

- modes 0–3 through CPOL/CPHA configuration;
- MSB-first or LSB-first transfer;
- 125 kbps to 8 Mbps;
- simultaneous TX/RX EasyDMA;
- configurable over-read character `ORC`.

SPIM transaction behavior:

```text
START
→ TX EasyDMA reads TXD buffer
→ RX EasyDMA writes RXD buffer
→ ENDTX when TX buffer is consumed
→ ENDRX when RX buffer reaches its limit
→ END when both sides are finished
```

If `RXD.MAXCNT > TXD.MAXCNT`, SPIM transmits `ORC` for remaining clocks. If `TXD.MAXCNT > RXD.MAXCNT`, extra received bytes are discarded.

Chip select is normally managed through GPIO, GPIOTE/PPI, or the driver. It must remain active over the complete transaction required by the slave.

SPIS uses a semaphore-style ACQUIRE/RELEASE model to prepare buffers before the external master asserts chip select. `STATUS` reports:

- TX over-read; and
- RX overflow.

`ORC` defines the byte shifted out when the master clocks beyond the prepared TX buffer. `DEF` is used for an ignored transaction according to SPIS behavior.

## 14. SAADC

SAADC provides eight configurable channels and writes results to RAM through EasyDMA.

Configuration areas include:

- positive/negative input selection;
- single-ended or differential mode;
- gain;
- reference;
- acquisition time;
- resistor configuration;
- resolution;
- oversampling;
- result buffer.

Single-buffer measurement flow:

```text
configure channels
→ RESULT.PTR / RESULT.MAXCNT
→ ENABLE
→ START task
→ wait STARTED
→ SAMPLE task, manually or through PPI
→ END when result buffer is complete
→ STOP
→ wait STOPPED
→ DISABLE
```

The ADC code must be interpreted using the configured reference, gain, resolution, and measurement mode. A generic conceptual conversion is:

```text
V_input ≈ code / full_scale_code × V_reference / gain
```

The exact signed range and full-scale behavior must follow the Product Specification for the selected channel mode.

Accuracy depends on source impedance and acquisition time. High-impedance sources may require longer acquisition or an external buffer.

Offset calibration must be performed only in a sequence allowed by the Product Specification and silicon errata. Do not trigger `CALIBRATEOFFSET` while sampling is active.

## 15. COMP and TEMP

### 15.1 COMP

COMP compares an analog input with a selected reference or threshold arrangement and can generate UP, DOWN, or CROSS events.

Use cases:

- battery or sensor threshold detection;
- wake-up threshold;
- zero-crossing-style input event;
- low-CPU monitoring through PPI.

Comparator hysteresis reduces chatter in noisy environments. Revision-specific errata must be checked because particular fast input transitions can cause increased current on affected revisions.

### 15.2 TEMP

TEMP measures die temperature, not ambient air or process temperature.

Conceptual sequence:

```text
clear DATARDY
→ START
→ wait DATARDY
→ read TEMP
→ STOP
```

Factory piecewise calibration coefficients reside in FICR and are consumed by the supported implementation. Do not copy coefficients between devices.

Die temperature can be useful for diagnostics and compensation but is influenced by CPU, radio, regulator, and package self-heating.

## 16. PWM

PWM0 provides four output channels and sequence playback through EasyDMA.

PWM is built around:

- a countertop value defining the PWM period;
- a prescaler defining the base clock;
- decoder mode defining how RAM values map to output channels;
- one or two RAM sequences;
- loop and shortcut control.

Conceptual sequence:

```text
RAM duty-cycle sequence
→ SEQ[n].PTR / CNT / REFRESH / ENDDELAY
→ configure PRESCALER, COUNTERTOP, MODE, DECODER
→ map output pins and polarity
→ ENABLE
→ SEQSTART task
→ SEQEND / LOOPSDONE / STOPPED events
```

Sequence buffers must remain in RAM until the corresponding sequence-end event.

Before enabling PWM, set each output GPIO to its required idle level. Before disabling, stop the sequence and wait for STOPPED to avoid a truncated output state.

## 17. PDM

PDM interfaces to a digital microphone and transfers signed audio samples to RAM through EasyDMA.

Driver requirements:

- configure clock and data pins;
- select clock frequency and gain;
- use RAM sample buffers;
- prepare the next buffer before the current buffer ends;
- handle STARTED, END, and STOPPED events;
- budget RAM, processing throughput, and radio coexistence;
- stop and disable PDM when audio capture is inactive.

Recommended double-buffer flow:

```text
buffer A active
→ STARTED event
→ program buffer B
→ END event for A
→ process A while B is active
→ program A again
```

Audio capture can dominate memory bandwidth and power. Measure the complete microphone + PDM + CPU processing duty cycle.

## 18. QDEC

QDEC decodes two-phase quadrature signals and accumulates position changes without GPIO polling.

Important outputs include:

- sample events;
- report-ready events;
- accumulated movement;
- double-transition error information.

QDEC is appropriate for slow encoders, wheels, and user controls. It does not replace electrical filtering or debounce. Invalid phase transitions caused by contact bounce or noise must be included in the quality model.

Use PPI/interrupt reporting at a suitable interval rather than waking the CPU for every edge.

## 19. RNG, ECB, CCM, and AAR

### 19.1 RNG

RNG produces bytes and raises `VALRDY` for each available value. Bias correction may affect output rate. Use the selected stack/security API when random data is needed for keys, nonces, or protocol security.

### 19.2 ECB

ECB performs an AES-128 block operation. ECB is a primitive, not a secure application message format by itself.

### 19.3 CCM and AAR

CCM and AAR share peripheral ID 15 and are normally managed by the BLE stack:

- CCM supports BLE packet encryption/authentication;
- AAR resolves BLE private addresses.

Application code must not claim ID 15 or related PPI resources while the stack owns them.

## 20. EGU and SWI

EGU and SWI provide software-triggered event/interrupt mechanisms. EGU0/SWI0 and EGU1/SWI1 share IDs.

Typical uses:

- defer work from a high-priority ISR;
- signal between platform modules;
- convert software actions into event-driven dispatch;
- provide task/event endpoints for PPI-based designs.

Do not use EGU/SWI as an unbounded work queue. The event only indicates that work is pending; the queue and overflow policy remain software responsibilities.

## 21. Peripheral Stop and Release Rules

| Peripheral | Required completion before disable/release |
|---|---|
| UARTE TX | `TXSTOPPED` after `STOPTX` if issued |
| UARTE RX | `RXTO`; process `ENDRX` and optional `FLUSHRX` |
| TWIM | `STOPPED` after STOP; resume first if suspended |
| TWIS | Transaction stopped/released according to slave state |
| SPIM | `END` for normal completion or `STOPPED` after STOP |
| SPIS | End/release semaphore and inspect status |
| SAADC | `STOPPED` after STOP |
| PWM | `STOPPED` after STOP |
| PDM | `STOPPED` after STOP |
| TIMER/RTC | STOP before reconfiguring state-sensitive fields |
| PPI | Disable channel before changing EEP/TEP or releasing endpoints |

After stopping:

1. disable interrupts;
2. disconnect PPI;
3. clear events/errors;
4. disconnect `PSEL` registers if required;
5. disable the peripheral;
6. return GPIO to a safe state;
7. release RAM buffers and resource ownership.

## 22. Recommended Driver Architecture

```text
Application services
└── asynchronous requests and completion events

Peripheral service layer
├── validates state and parameters
├── allocates timers / PPI / GPIOTE
├── owns timeout and recovery policy
└── converts hardware events into typed results

Low-level driver
├── configures one peripheral instance
├── owns IRQ and EasyDMA buffers
├── applies errata workarounds
└── implements start / stop / abort state machine

Platform resource manager
├── shared peripheral IDs
├── pins
├── interrupt priorities
├── PPI / GPIOTE / CC channels
└── clock and power requests
```

Suggested transaction result:

```c
typedef struct {
    int status;
    size_t transferred;
    uint32_t hardware_error;
    uint32_t timestamp;
} peripheral_result_t;
```

Return byte count and hardware status together so information is not lost between separate API calls.

## 23. Main Technical Risks

| Risk | Consequence | Required control |
|---|---|---|
| Event is not cleared before restart | False completion or missed transaction boundary | Clear and read back every used event. |
| ISR exits before interrupt clear propagates | Immediate spurious ISR re-entry | Perform volatile register read-back. |
| Shared peripheral ID is double-owned | Register and IRQ corruption | Central resource ownership. |
| EasyDMA pointer targets flash/expired buffer | HardFault or corrupted transfer | RAM-range and lifetime validation. |
| PPI remains connected after driver release | Unexpected task trigger | Disable and disconnect PPI first. |
| GPIO SENSE is already active before System OFF | Immediate wake loop | Verify inactive level before sleep. |
| RTC prescaler is written while running | New value has no effect | Stop RTC before configuration. |
| TWIM error path does not issue STOP | Bus/peripheral remains active | Resume if needed, STOP, wait STOPPED. |
| UARTE RX uses RXDRDY as DMA completion | CPU reads data before DMA ownership ends | Use ENDRX/RXTO and AMOUNT. |
| SPIM TX/RX lengths are assumed equal | ORC transmission or discarded bytes | Define full-duplex length semantics. |
| SAADC acquisition is too short | Gain/linearity error | Match acquisition time to source impedance. |
| Watchdog fed from periodic ISR | Application deadlock is hidden | Feed only after health aggregation. |
| Peripheral is disabled before STOPPED | Truncated transfer or retained power state | Follow documented stop sequence. |
| Errata workaround is omitted | Revision-specific failure | Review actual FICR variant and errata. |

## 24. Implementation Checklist

- [ ] Keep one owner for each peripheral ID, pin, IRQ, PPI, GPIOTE, and CC channel.
- [ ] Configure pins and registers before enabling the peripheral.
- [ ] Clear stale events and errors before every start.
- [ ] Read back cleared event/interrupt registers before leaving an ISR.
- [ ] Place every EasyDMA buffer in RAM and define its lifetime.
- [ ] Use END/STOPPED-type events before releasing DMA buffers.
- [ ] Disable PPI before changing endpoints or peripheral modes.
- [ ] Use GPIO atomic SET/CLR registers.
- [ ] Verify SENSE is inactive before System OFF.
- [ ] Use TIMER for precise timing and RTC for low-power scheduling.
- [ ] Handle counter wraparound explicitly.
- [ ] Feed WDT only after aggregated health checks.
- [ ] Use UARTE ENDRX/RXTO/AMOUNT for receive ownership.
- [ ] Ensure TWIM always reaches STOPPED, including error paths.
- [ ] Define SPIM/SPIS over-read, overflow, and length behavior.
- [ ] Match SAADC acquisition time to source impedance.
- [ ] Stop and disable unused peripherals for low power.
- [ ] Review peripheral anomalies for the actual silicon revision.
- [ ] Test cancel, timeout, error, and simultaneous-event paths.
- [ ] Measure current with the final peripheral duty cycle.

## 25. References

1. [nRF52810 Product Specification](https://docs.nordicsemi.com/r/bundle/ps_nrf52810/)
2. [nRF52810 Revision 3 Errata](https://docs.nordicsemi.com/bundle/errata_nRF52810_Rev3/page/ERR/nRF52810/Rev3/latest/err_810.html)
3. [Nordic nRF52810 documentation resources](https://docs.nordicsemi.com/r/bundle/additionalresources/page/additionalresources/nrf52-series/nrf52810)
