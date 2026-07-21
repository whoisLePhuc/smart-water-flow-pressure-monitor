# Pre-bring-up implementation status

This file separates three different claims:

- **Implemented**: production source exists and compiles for the portable
  target.
- **Integrated**: upstream and downstream owners exchange real repository
  transactions/events through the composition path.
- **Verified**: an automated test exercised the stated path on the named
  platform.

No row marked Linux-verified should be interpreted as STM32 hardware-verified.

| Capability | Implemented | Integrated | Linux verification | STM32 verification |
| --- | --- | --- | --- | --- |
| Shared I2C priority queue, completion, timeout, cancel, recovery | Yes | Pressure + F-RAM clients | Unit + pressure/storage integration | Pending |
| ZSSC3241 command, status and U24 decode | Yes | Pressure service | Driver + end-to-end test | Pending |
| SPI transaction state machine | Yes | Portable clients | Unit test | Pending |
| MAX35103 TOF frame decoder | Yes | Flow service via completion event | Driver + pipeline test | Pending |
| Temperature freshness/result pairing | Yes | Flow acceptance gate | Processing + pipeline test | Pending |
| Physical flow formula and calibration binding | Yes | Flow publication | Unit + pipeline test | Pending qualification |
| Flow → volume → leak | Yes | Single `RepoWriteTxn` | Atomic integration test | Pending |
| Leak pressure evidence | Yes | Leak evaluation | Leak regression tests | Pending thresholds |
| FM24CL04B async read/write/probe and `0x0FF`/`0x100` split | Yes | Shared I2C manager | Driver + wire-level integration | Pending |
| F-RAM invalidate/write/readback/commit-last | Yes | Async storage service | End-to-end I2C pipeline + injected failure | Pending |
| True A/B slot rotation and newest async restore | Yes | Async storage service | Two-generation + boot/power-loss tests | Pending |
| Evidence-backed FSM guards | Yes | Event loop | Reliability + FSM tests | Pending evidence owners |
| FSM action executor | Yes | Event loop | Reliability test | Board actions pending |
| ISR event posting contract | Yes | Queue | Critical-section unit test | IRQ binding pending |
| Execution-time/event/step budgets | Yes | Event loop | Compile + runtime regression | Timing pending |
| STM32 ADC/I2C/SPI adapters | Skeleton | Board HAL not bound | Contract/compile test | Pending |
| RTC/GPIO/UART/STOP 2/watchdog ports | Contract | No | Header compile | Pending |

## Hardware-dependent acceptance work

The following cannot be closed before a real board and qualified sensor setup
exist:

- timer drift and wrap behavior;
- electrical I2C/SPI timing, IRQ polarity and DMA/cache interaction;
- MAX35103 register-address/read-command binding for the selected board;
- ZSSC3241 EOC timing and recovery under bus faults;
- sensor calibration coefficients and production acceptance thresholds;
- F-RAM write-protect and power-loss behavior;
- RTC accuracy, STOP 2 current/wake behavior and watchdog reset timing.

CI treats normal tests, architecture checks and ASan/UBSan tests as required.

## Portable F-RAM boundary

The portable path is now complete and host-verified:

```text
StorageService -> StoragePort -> FramDriver -> I2cBusManager -> I2cPort
```

`FramDriver` uses the FM24CL04B block-select address bit (`0x50`/`0x51` for
the default strap), an 8-bit word address and bounded asynchronous chunks.
`StorageService` advances only from matching completion tokens and preserves
the prior valid A/B slot until the new slot's commit byte has been verified.
The remaining boundary is the real STM32 HAL binding and physical-board
verification listed above.
