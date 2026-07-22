# STM32 F-RAM test firmware

This directory builds a standalone `test_fram_storage.elf`. It verifies the
real STM32L433 I2C1 path and FM24CL04B instead of reusing the host-side fake
wire. The production `swfpm_app.elf` remains a separate target.

Source baseline: repository `main` commit `369a8a3` plus the application boot
state changes in this worktree.

## Test layers

| Layer         | Scope                                                                                                                   |
| ------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `unit`        | `i2c_port_stm32` mapping, busy state, request snapshot, cancel/recover and exact-once completion with a fake HAL vtable |
| `contract`    | Real I2C1 TX, repeated-start RX, deferred IRQ completion and NACK mapping                                               |
| `integration` | Real `I2cBusManager -> FramDriver -> STM32 I2C1 -> FM24CL04B` path                                                      |
| `system`      | Real `AppComposition -> StorageService -> FramDriver` checkpoint and restore path                                       |

The HIL suite currently contains 16 tests. The portable application boot policy
also has a native-host test covering four terminal restore outcomes.

## Native application boot test

`tests/host/test_app_composition_boot.c` runs the production
`AppComposition`, bus manager, F-RAM driver, codec and storage service over an
in-memory F-RAM wire. It verifies canonical empty storage, a valid checkpoint,
corrupt storage, an I/O failure, exact-once result consumption and the
`runtime_ready` gate.

Build it independently from the STM32 cross-build:

```bash
cmake -S 3.stm32/tests/host -B 3.stm32/build-host-tests
cmake --build 3.stm32/build-host-tests
ctest --test-dir 3.stm32/build-host-tests --output-on-failure
```

## Implemented tests

### Unit tests

The unit tests exercise `i2c_port_stm32` with a fake asynchronous HAL vtable.
They do not access the physical I2C bus.

| ID          | Test                                 | Verified behavior                                                                                                      |
| ----------- | ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------- |
| `UT-I2C-01` | HAL submission status mapping        | Maps HAL `OK`, `BUSY` and error results to the corresponding portable `PortStatus` values.                             |
| `UT-I2C-02` | Busy state and exact-once completion | Rejects a second request while one is active, preserves the submitted request snapshot and emits only one completion.  |
| `UT-I2C-03` | Cancel identity and recovery         | Rejects cancellation with the wrong transaction identity and confirms that recovery clears the adapter's active state. |

### Platform contract tests

The contract tests use the real STM32 I2C1 adapter and IRQ/deferred-completion
path. They verify the contract between the portable I2C port and STM32 HAL.

| ID          | Test                           | Verified behavior                                                                                                                             |
| ----------- | ------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------- |
| `CT-I2C-01` | Deferred TX-then-RX completion | Completes a combined transaction through IRQ handling and deferred polling without delivering the upper-layer callback directly from the ISR. |
| `CT-I2C-02` | Write and random read          | Verifies TX-only writes and the repeated-start sequence used by F-RAM random reads.                                                           |
| `CT-I2C-03` | Address NACK mapping           | Confirms that an invalid/unavailable slave address is reported as a portable transaction failure.                                             |

### F-RAM integration tests

These tests exercise the complete
`I2cBusManager -> FramDriver -> STM32 I2C1 -> FM24CL04B` path on the board.

| ID           | Test                     | Verified behavior                                                                                                                    |
| ------------ | ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------ |
| `IT-FRAM-01` | Device probe             | Probes both FM24CL04B block addresses, `0x50` and `0x51`.                                                                            |
| `IT-FRAM-02` | Reserved-area round trip | Writes and reads deterministic patterns with lengths `1`, `16`, `31`, `32`, `33` and `64` bytes, including driver chunk boundaries.  |
| `IT-FRAM-03` | Cross-block access       | Writes 40 bytes from `0x0F8` and verifies correct access across logical address `0x100` and slave-address transition `0x50 -> 0x51`. |
| `IT-FRAM-04` | NACK recovery            | Forces an address NACK, recovers the peripheral and successfully probes the F-RAM again.                                             |
| `IT-FRAM-05` | Timeout and bus reuse    | Verifies deadline timeout, cancellation, rejection of a late completion and successful reuse of the bus by a later request.          |
| `IT-FRAM-06` | Shared-bus serialization | Confirms that a second I2C client and the F-RAM are serialized rather than executed concurrently.                                    |

### Storage system tests

The system tests reconstruct the production object graph and exercise
`AppComposition -> StorageService -> FramDriver -> STM32 I2C1 -> FM24CL04B`.

| ID           | Test                   | Verified behavior                                                                                               |
| ------------ | ---------------------- | --------------------------------------------------------------------------------------------------------------- |
| `ST-STOR-01` | Automatic boot restore | Saves a checkpoint, reconstructs the graph, starts through `AppComposition` and retains all fields.             |
| `ST-STOR-02` | Interrupted commit     | Simulates interruption before the commit byte and confirms that the previously valid A/B slot remains selected. |
| `ST-STOR-03` | Repeated checkpoints   | Alternates A/B slots across repeated checkpoints and confirms that the final committed record is restored.      |
| `ST-STOR-04` | Empty boot              | Confirms that two canonical empty slots reach storage-ready without enabling the production runtime.             |

The test suite therefore covers adapter state handling, HAL contract behavior,
physical F-RAM communication, shared-bus coordination and persistent A/B
checkpoint recovery. It does not replace external electrical fault-injection
tests; the remaining limitations are described in [Interpretation](#interpretation).

## Build

Tests are built by default (`SWFPM_BUILD_FRAM_HIL_TESTS=ON`). From the
repository root:

```bash
cmake -G Ninja -S 3.stm32 -B 3.stm32/build \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/3.stm32/cmake/gcc-arm-none-eabi.cmake"

cmake --build 3.stm32/build -j
```

This produces two binaries:

| Binary                              | Purpose              |
| ----------------------------------- | -------------------- |
| `build/swfpm_app.elf`               | Production firmware  |
| `build/tests/test_fram_storage.elf` | F-RAM HIL test suite |

To build only the production firmware, disable tests:

```bash
cmake -S 3.stm32 -B 3.stm32/build-prod \
  -DSWFPM_BUILD_FRAM_HIL_TESTS=OFF \
  ...
```

## Run the suite

Flash `test_fram_storage.elf` with the normal STM32 programmer/debugger.
Open USART2 at `115200 8N1`.

## Destructive tests

Five tests overwrite F-RAM regions and are skipped by default. Enable them
with `-DSWFPM_ENABLE_DESTRUCTIVE_STORAGE_TESTS=ON`:

| Test         | What it overwrites                                                                        |
| ------------ | ----------------------------------------------------------------------------------------- |
| `IT-FRAM-03` | `0x0F8..0x11F` temporarily; saves and restores its original 40 bytes on normal completion |
| `ST-STOR-01` | Volume slots `0x140..0x1BF`; does not restore previous data                               |
| `ST-STOR-02` | Volume slots `0x140..0x1BF`; does not restore previous data                               |
| `ST-STOR-03` | Volume slots `0x140..0x1BF`; does not restore previous data                               |
| `ST-STOR-04` | Volume slots `0x140..0x1BF`; leaves both slots in the canonical empty state                |

Only run destructive tests on a dedicated test board. The system tests
intentionally clear both volume slots and do not restore previous
application data.

## Previous HIL baseline results

The output below records the 15-test baseline before `ST-STOR-04` and the new
application-owned boot assertions were added. Run the 16-test image on the
board before marking this phase HIL-verified.

Two execution profiles have been verified on the STM32 board:

| Profile                           | Result                           | Meaning                                                                                                    |
| --------------------------------- | -------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| Non-destructive (`destructive=0`) | `11 passed, 0 failed, 4 skipped` | All tests that preserve application storage passed; the four destructive tests were intentionally skipped. |
| Full suite (`destructive=1`)      | `15 passed, 0 failed, 0 skipped` | All unit, contract, integration and storage system tests passed on the dedicated test board.               |

The following is the recorded full-suite UART output:

```
SUITE|fram_hil|START|tests=15|destructive=1
TEST|UT-I2C-01|START|HAL submission status mapping
TEST|UT-I2C-01|PASS|elapsed_us=0
TEST|UT-I2C-02|START|busy, snapshot and exact-once completion
TEST|UT-I2C-02|PASS|elapsed_us=0
TEST|UT-I2C-03|START|cancel identity and recovery state
TEST|UT-I2C-03|PASS|elapsed_us=0
TEST|CT-I2C-01|START|TX-then-RX uses deferred exact-once completion
TEST|CT-I2C-01|PASS|elapsed_us=8000
TEST|CT-I2C-02|START|TX-only write and repeated-start read
TEST|CT-I2C-02|PASS|elapsed_us=8000
TEST|CT-I2C-03|START|address NACK maps to portable failure
TEST|CT-I2C-03|PASS|elapsed_us=7000
TEST|IT-FRAM-01|START|probe FM24CL04B blocks 0x50 and 0x51
TEST|IT-FRAM-01|PASS|elapsed_us=13000
TEST|IT-FRAM-02|START|reserved-area patterns and chunk boundaries
TEST|IT-FRAM-02|PASS|elapsed_us=131000
TEST|IT-FRAM-03|START|read/write across logical address 0x100
TEST|IT-FRAM-03|PASS|elapsed_us=22000
TEST|IT-FRAM-04|START|NACK then peripheral recovery and reprobe
TEST|IT-FRAM-04|PASS|elapsed_us=18000
TEST|IT-FRAM-05|START|deadline timeout, late callback and bus reuse
TEST|IT-FRAM-05|PASS|elapsed_us=15000
TEST|IT-FRAM-06|START|shared bus serializes a second client and F-RAM
TEST|IT-FRAM-06|PASS|elapsed_us=14000
TEST|ST-STOR-01|START|checkpoint, software reboot and volume restore
TEST|ST-STOR-01|PASS|elapsed_us=77000
TEST|ST-STOR-02|START|interrupted commit retains previous valid A/B slot
TEST|ST-STOR-02|PASS|elapsed_us=91000
TEST|ST-STOR-03|START|repeated A/B checkpoints and final restore
TEST|ST-STOR-03|PASS|elapsed_us=1992000
SUMMARY|passed=15|failed=0|skipped=0
```

## Coverage

| ID           | Purpose                                            | Destructive | Elapsed |
| ------------ | -------------------------------------------------- | ----------: | ------: |
| `UT-I2C-01`  | HAL submission status mapping                      |          No |    0 µs |
| `UT-I2C-02`  | Busy, request snapshot and exact-once completion   |          No |    0 µs |
| `UT-I2C-03`  | Cancel identity and recovery state                 |          No |    0 µs |
| `CT-I2C-01`  | Deferred TX-then-RX completion                     |          No |    8 ms |
| `CT-I2C-02`  | TX-only write and repeated-start read              |          No |    8 ms |
| `CT-I2C-03`  | NACK mapping                                       |          No |    7 ms |
| `IT-FRAM-01` | Probe blocks `0x50` and `0x51`                     |          No |   13 ms |
| `IT-FRAM-02` | Patterns and lengths `1,16,31,32,33,64`            |          No |  131 ms |
| `IT-FRAM-03` | Cross logical address `0x0FF/0x100`                |         Yes |   22 ms |
| `IT-FRAM-04` | NACK, recover and reprobe                          |          No |   18 ms |
| `IT-FRAM-05` | Timeout, late callback and bus reuse               |          No |   15 ms |
| `IT-FRAM-06` | Two clients serialized by shared bus               |          No |   14 ms |
| `ST-STOR-01` | Checkpoint, software reboot and restore            |         Yes |   77 ms |
| `ST-STOR-02` | Discard RAM before commit byte and retain old slot |         Yes |   91 ms |
| `ST-STOR-03` | Repeated A/B checkpoint soak and final restore     |         Yes | 1992 ms |
| `ST-STOR-04` | Canonical empty application boot                   |         Yes | Pending |

## Hardware assumptions

* MCU: STM32L433RCT6.
* I2C1: PB6 SCL and PB7 SDA with external pull-ups.
* FM24CL04B base address: `0x50`; block addresses are `0x50/0x51`.
* USART2: PA2 TX and PA3 RX at 115200 baud.
* This version follows the current firmware and drives PB8 low to disable F-RAM
  write protection. If the final schematic straps WP to ground, remove the PB8
  initialization from `fram_hil_main.c` together with the production code.

## Interpretation

Passing this suite provides evidence for the STM32 software and HIL path. A
physical power-cut test and an SDA-held-low bus-unwedge test still require an
external fixture; software alone cannot reliably create those electrical
conditions. `ST-STOR-02` reconstructs the complete production object graph and
therefore verifies commit-last behavior, but it is a software reboot rather
than loss of board power.
