# Verification report

Date: 2026-07-16  
Baseline repository commit: `041d456fd07ab89faf030376c181be104b581e46`

## Local verification completed

- Architecture enforcement: 0 errors, 0 warnings.
- `git diff --check`: passed.
- Production-source compile check: 65/65 C source files accepted with C11,
  `-Wall -Wextra -Werror`.
- 24 distinct unit/integration/contract executables were linked and run with
  AddressSanitizer and UndefinedBehaviorSanitizer; all passed.
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
- mode guards, action executor and STM32 async adapter contracts;
- event mediator and Linux mode-transition integration.

## Tooling boundary

CMake was not installed in the artifact-building sandbox, so the full CMake
configure/build/CTest graph was not executed locally. GitHub Actions is
configured to require configure, warning-clean build, architecture check,
CTest, and a second ASan/UBSan build and CTest run.

## Datasheet cross-check

- Renesas ZSSC3241 general status byte and `0xAA` measurement response:
  <https://www.renesas.com/en/document/dst/zssc3241-datasheet>
- Analog Devices MAX35103 conversion-result register definitions:
  <https://www.analog.com/media/en/technical-documentation/data-sheets/max35103.pdf>

## Not verified

No claims are made for electrical behavior, timing, calibration accuracy,
STOP 2 current, RTC accuracy, F-RAM power-loss behavior or watchdog reset on a
physical STM32 board. Those remain bring-up acceptance items.
