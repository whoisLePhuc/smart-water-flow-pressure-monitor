# Verification report

Date: 2026-07-21
Baseline repository commit: `e7130f1edc35761edb3207aa7840bedccabb1d00`

## Local verification completed

- Architecture enforcement: 0 errors, 0 warnings.
- `git diff --check`: passed.
- The full CMake build completed with C11, `-Wall -Wextra -Werror`.
- CTest executed 62 unit, integration, contract, system and architecture
  checks with AddressSanitizer and UndefinedBehaviorSanitizer; 62/62 passed.
- LeakSanitizer itself cannot inspect `/proc` in this sandbox, so leak checking
  was disabled while AddressSanitizer and UndefinedBehaviorSanitizer remained
  active.

The executed set covered:

- I2C and SPI bus managers;
- ISR event queue and repository transactions;
- ZSSC3241 and MAX35103 decoders;
- flow and pressure processing;
- pressure end-to-end pipeline;
- atomic flow/volume/leak pipeline;
- volume admission/arithmetic/duplicate/reset behavior;
- leak config/tracker/state behavior;
- storage codec, A/B selection and service rotation;
- FM24CL04B page-address mapping, bounded chunking, timeout, cancellation and
  stale-completion rejection;
- the complete asynchronous `StorageService -> FramDriver -> I2cBusManager`
  path, including commit-last, injected I/O failure and boot restore;
- mode guards, action executor and STM32 async adapter contracts;
- event mediator and Linux mode-transition integration.

## Tooling boundary

The full CMake configure/build/CTest graph was executed locally. GitHub
Actions remains configured to repeat configure, warning-clean build,
architecture check, CTest and ASan/UBSan verification.

## Datasheet cross-check

- Renesas ZSSC3241 general status byte and `0xAA` measurement response:
  <https://www.renesas.com/en/document/dst/zssc3241-datasheet>
- Analog Devices MAX35103 conversion-result register definitions:
  <https://www.analog.com/media/en/technical-documentation/data-sheets/max35103.pdf>

## Not verified

No claims are made for electrical behavior, timing, calibration accuracy,
STOP 2 current, RTC accuracy, F-RAM power-loss behavior or watchdog reset on a
physical STM32 board. Those remain bring-up acceptance items.
