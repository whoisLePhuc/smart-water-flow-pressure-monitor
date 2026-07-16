---
title: nRF52810 Memory and Register Notes
source_specification: nRF52810 Product Specification v1.2
purpose: Firmware-oriented notes for memory layout, NVMC, FICR, UICR, BPROT, APPROTECT, and peripheral register access
status: Reviewed against the official Nordic Product Specification and Revision 3 Errata
device: nRF52810
---

# nRF52810 Memory and Register Notes

> **Source of truth:** [nRF52810 Product Specification](https://docs.nordicsemi.com/r/bundle/ps_nrf52810/), version 1.2, and the errata document for the actual silicon revision. This document explains implementation consequences and safe workflows; it does not replace the complete memory map, register field definitions, timing limits, or access-protection procedure in Nordic documentation.

Related notes:

- [`NRF52810_Technical_Summary.md`](NRF52810_Technical_Summary.md): CPU, memory limits, EasyDMA, shared peripherals, clocks, power, and radio overview.
- [`NRF52810_Peripheral_Notes.md`](NRF52810_Peripheral_Notes.md): task/event flows, interrupt handling, EasyDMA, and peripheral-specific registers.
- [`NRF52810_Clock_Power_Notes.md`](NRF52810_Clock_Power_Notes.md): RAM power sections, reset behavior, retention, and low-power states.
- [`NRF52810_Boot_Debug_Notes.md`](NRF52810_Boot_Debug_Notes.md): startup, UICR image, SWD, APPROTECT, recovery, and production programming.

## 1. Memory Architecture

The nRF52810 contains 192 KB flash and 24 KB RAM.

```text
Flash: 0x00000000 ... 0x0002FFFF
├── 48 pages
├── 4 KB per page
└── one BPROT protection region per 4 KB page

RAM: 0x20000000 ... 0x20005FFF
├── 24 KB total
├── 3 RAM AHB slaves
└── 2 × 4 KB sections per AHB slave
```

The flash size can be derived at runtime from FICR:

```c
uint32_t flash_bytes = NRF_FICR->CODEPAGESIZE * NRF_FICR->CODESIZE;
```

For nRF52810:

```text
CODEPAGESIZE = 0x1000 = 4096 bytes
CODESIZE     = 0x30   = 48 pages
Flash size   = 4096 × 48 = 196608 bytes = 192 KB
```

Code RAM and Data RAM are two mappings of the same physical RAM. A linker script that places executable code in RAM must prevent that region from overlapping `.data`, `.bss`, stack, heap, retained data, or EasyDMA buffers.

## 2. Main Address Regions

| Address range / base | Region | Firmware meaning |
|---|---|---|
| `0x00000000` | Flash | Vector table, code, constants, settings, bootloader, and DFU image |
| `0x10000000` | FICR | Factory-programmed, read-only device information |
| `0x10001000` | UICR | User-programmable nonvolatile configuration |
| `0x20000000` | Data RAM | CPU data and the valid buffer region for EasyDMA |
| `0x40000000` | Peripheral space | Tasks, events, interrupts, status, and configuration registers |
| `0x50000000` | GPIO port | P0 GPIO register interface |

Do not hard-code a register address when the Nordic MDK/CMSIS header provides the device-specific symbol. Numeric addresses remain useful for debugging, memory dumps, linker review, and production-image inspection.

## 3. Register Access Conventions

Nordic register tables identify the access type of each register or field.

| Access | Meaning | Driver rule |
|---|---|---|
| `R` | Read-only | Never write, even when preserving neighboring bits appears possible. |
| `W` | Write-only | Do not use read-modify-write. Write only the documented command value. |
| `RW` | Read/write | Mask fields and preserve documented unrelated bits. |
| Task register | Write to trigger an action | Normally write `1`; do not treat it as retained state. |
| Event register | Hardware sets an event | Clear using the peripheral's documented sequence before enabling/reusing it. |
| `INTENSET` | Set interrupt-enable bits | Writing `1` enables; writing `0` does not disable. |
| `INTENCLR` | Clear interrupt-enable bits | Writing `1` disables; writing `0` has no effect. |

Register code should use masks rather than compiler-dependent C bitfields:

```c
static inline uint32_t field_replace(uint32_t reg,
                                     uint32_t mask,
                                     uint32_t position,
                                     uint32_t value)
{
    return (reg & ~mask) | ((value << position) & mask);
}
```

Important rules:

- preserve reserved bits as required by the Product Specification;
- do not infer event-clear behavior from another nRF52 peripheral;
- do not read a write-only task register to determine hardware state;
- use `volatile` device-header definitions;
- include memory barriers where required by the CPU, SDK, or errata workaround;
- clear stale events before starting a new transaction.

## 4. Flash Layout

Flash pages are numbered `0` through `47`.

```text
page_start = page_number × 0x1000
page_end   = page_start + 0x0FFF
```

Examples:

| Page | Address range |
|---:|---|
| 0 | `0x00000000–0x00000FFF` |
| 1 | `0x00001000–0x00001FFF` |
| 46 | `0x0002E000–0x0002EFFF` |
| 47 | `0x0002F000–0x0002FFFF` |

A production linker map should assign page-aligned regions for:

```text
vector table / application
protocol stack or RTOS
persistent settings
bootloader
DFU metadata or secondary image strategy
```

The exact partition is project-specific. This component document does not prescribe fixed application, settings, or bootloader addresses.

Before accepting a partition, verify:

- every region is inside `0x00000000–0x0002FFFF`;
- erase boundaries are 4 KB aligned;
- BPROT regions match the intended protected pages;
- the bootloader and stack use the same partition definition;
- persistent settings do not overlap application update space;
- the final HEX image contains the expected UICR records.

## 5. RAM Layout and Retention

The 24 KB RAM consists of six 4 KB sections. Each section can be controlled for accessibility and retention through POWER registers.

Typical logical partitioning is:

```text
low RAM
├── protocol-stack memory
├── .data / .bss
├── heap
├── stacks
├── EasyDMA buffers
└── retained state
high RAM
```

The actual order is defined by the linker and software stack.

RAM design rules:

- EasyDMA pointers must reference Data RAM.
- A `const` object is normally stored in flash and is not a valid EasyDMA source.
- Stack high-water marks must be measured under worst-case nested interrupts.
- Retained data must be placed in a known section and validated after reset.
- RAM powered off in System ON must not be referenced by CPU or DMA.
- RAM not retained in System OFF must be reinitialized after wake-up reset.

A retained structure should contain format validation:

```c
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t reset_counter;
    uint32_t crc32;
} retained_header_t;
```

Retention does not replace persistent flash storage. Brownout, reset conditions, or an incomplete retention configuration can make RAM contents invalid.

## 6. NVMC Overview

NVMC controls writes and erases for flash and UICR.

| Item | Value / behavior |
|---|---|
| Base address | `0x4001E000` |
| `READY` offset | `0x400` |
| `CONFIG` offset | `0x504` |
| `ERASEPAGE` offset | `0x508` |
| `ERASEALL` offset | `0x50C` |
| `ERASEUICR` offset | `0x514` |
| `ERASEPAGEPARTIAL` offset | `0x518` |
| `ERASEPAGEPARTIALCFG` offset | `0x51C` |
| Word write | Full 32-bit word at a word-aligned address |
| Page erase | 4 KB page |
| Page endurance | 10,000 erase cycles according to the Product Specification |
| Writes before erase | A 32-bit word can be written two times before page erase |
| Typical word-write time | 41 µs under the specified condition |
| Typical page-erase time | 85 ms under the specified condition |
| Typical erase-all time | 169 ms under the specified condition |

Timing values are specification values for defined conditions and must not be converted into fixed delay loops. Poll `READY` and apply a system-level timeout.

## 7. NVMC Access Modes

`NVMC.CONFIG.WEN` selects the access mode:

| Value | Mode | Meaning |
|---:|---|---|
| `0` | `Ren` | Read-only |
| `1` | `Wen` | Write enabled |
| `2` | `Een` | Erase enabled |

Write and erase must never be enabled at the same time. The recommended sequence is:

```text
read-only
→ enable exactly one operation mode
→ wait until READY
→ perform one controlled operation
→ wait until READY
→ return to read-only
```

Do not leave NVMC in write or erase mode after the operation.

## 8. Writing Flash

NVMC writes only full 32-bit words at word-aligned addresses.

Flash is erased to all `1` bits. Programming can only change bits from `1` to `0`:

```text
erased:      1111 1111
programmed:  1010 0011   valid, only 1→0 transitions
rewrite:     1110 0011   invalid, attempts 0→1 transition
```

Byte- or halfword-aligned writes cause a HardFault.

Conceptual write sequence:

```c
bool nrf52810_flash_write_word(uint32_t address, uint32_t value)
{
    if ((address & 0x3u) != 0u || address >= 0x00030000u) {
        return false;
    }

    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    *(volatile uint32_t *)address = value;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    return *(volatile uint32_t *)address == value;
}
```

This illustrates the hardware sequence only. Production code should use the selected SDK storage/NVMC API so that radio timing, critical sections, errata, and stack coexistence are handled correctly.

The CPU is halted during a flash word write. A BLE stack may impose additional restrictions on when direct NVMC operations are allowed.

## 9. Erasing Flash

`ERASEPAGE` receives the address of the first word in the page.

Conceptual sequence:

```text
validate page address and ownership
→ ensure no protected page is targeted
→ enable erase mode
→ wait READY
→ write page base address to ERASEPAGE
→ wait READY with timeout
→ return NVMC to read-only
→ verify erased words are 0xFFFFFFFF
```

Attempting to erase an address outside the code region can erase an unintended page or cause undefined behavior.

`ERASEALL` erases user flash and UICR but does not erase FICR. It is a recovery/production operation, not an application settings operation.

Partial page erase splits the erase time into shorter segments. The page contents are undefined until the accumulated partial-erase duration is sufficient for a complete erase. Partial erase:

- applies only to code flash;
- does not apply to UICR;
- still counts as one erase cycle when a full erase has accumulated;
- requires careful state persistence if interrupted.

## 10. FICR — Factory Information

FICR is factory programmed, read-only, and not erased by `ERASEALL`.

Base address:

```text
FICR = 0x10000000
```

Important fields:

| Register | Offset | Meaning |
|---|---:|---|
| `CODEPAGESIZE` | `0x010` | Flash page size in bytes |
| `CODESIZE` | `0x014` | Number of flash pages |
| `DEVICEID[0]` | `0x060` | Least-significant 32 bits of unique device identifier |
| `DEVICEID[1]` | `0x064` | Most-significant 32 bits of unique device identifier |
| `ER[0..3]` | `0x080–0x08C` | Encryption root words |
| `IR[0..3]` | `0x090–0x09C` | Identity root words |
| `DEVICEADDRTYPE` | `0x0A0` | Factory device-address type |
| `DEVICEADDR[0]` | `0x0A4` | Low 32 bits of 48-bit device address |
| `DEVICEADDR[1]` | `0x0A8` | Upper 16 used bits of device address |
| `INFO.PART` | `0x100` | Part code; nRF52810 is `0x52810` |
| `INFO.VARIANT` | `0x104` | Variant/hardware/production code encoded as ASCII |
| `INFO.PACKAGE` | `0x108` | Package option |
| `INFO.RAM` | `0x10C` | RAM size variant |
| `INFO.FLASH` | `0x110` | Flash size variant |
| `TEMP.*` | `0x404–0x444` | Factory temperature-calibration coefficients |

Firmware should use `INFO.PART`, `INFO.VARIANT`, and package/memory information for diagnostics and compatibility checks, not to replace build-time selection of the correct device header.

`DEVICEID` is useful for traceability but is not automatically a cryptographic secret. Do not use it alone as an authentication key.

`ER` and `IR` are factory security material used by Nordic protocol mechanisms. Application code must not log or export them casually.

Example 64-bit device identifier assembly:

```c
uint64_t device_id = ((uint64_t)NRF_FICR->DEVICEID[1] << 32)
                   | (uint64_t)NRF_FICR->DEVICEID[0];
```

## 11. UICR — User Information Configuration

UICR is nonvolatile configuration memory at:

```text
UICR = 0x10001000
```

Important groups:

| Register group | Offset | Purpose |
|---|---:|---|
| `NRFFW[0..12]` | `0x014–0x044` | Reserved for Nordic firmware design |
| `NRFHW[0..11]` | `0x050–0x07C` | Reserved for Nordic hardware design |
| `CUSTOMER[0..31]` | `0x080–0x0FC` | 32 customer words |
| `NRFMDK[0..7]` | `0x100–0x11C` | Reserved for Nordic MDK |
| `PSELRESET[0]` | `0x200` | nRESET pin mapping word 0 |
| `PSELRESET[1]` | `0x204` | nRESET pin mapping word 1 |
| `APPROTECT` | `0x208` | Access-port protection configuration |

Reserved Nordic/MDK words must not be used as application storage.

### 11.1 UICR write behavior

UICR is written through NVMC using the same `1→0`, aligned 32-bit programming model as flash.

New UICR configuration takes effect after reset.

To change a bit from `0` back to `1`, UICR must be erased. `ERASEUICR` erases the complete UICR region, not a single word. A replacement UICR image must therefore restore every required field.

### 11.2 Customer words

`CUSTOMER[0..31]` provides 128 bytes of nonvolatile customer space. Suitable examples include:

- board/configuration schema version;
- manufacturing identifier;
- hardware feature flags;
- immutable calibration metadata;
- public product identity data.

It is not suitable for frequently updated counters or logs because:

- UICR has flash-like write/erase limits;
- erasing one changed value erases all UICR configuration;
- recovery must restore reset/protection settings;
- changes take effect according to UICR/reset behavior.

Use normal flash settings pages for runtime configuration.

## 12. PSELRESET

`PSELRESET[0]` and `PSELRESET[1]` map the nRESET function. Both registers must contain the same value for the mapping to be valid.

For the nRF52810, the supported reset mapping uses GPIO `P0.21` according to the device configuration.

```text
PSELRESET[0] == PSELRESET[1]
→ valid nRESET mapping

PSELRESET[0] != PSELRESET[1]
→ no nRESET function is exposed on a GPIO
```

This duplication protects against an accidental partial configuration.

Production requirements:

- program both words in the same UICR image;
- verify both words after programming;
- reset and test physical nRESET behavior;
- confirm the pin is not also assigned to an application function;
- include the values in recovery and erase/reprogram workflows.

## 13. APPROTECT

`UICR.APPROTECT` configures debug access-port protection. The exact protection mechanism depends on the silicon revision and can require both hardware and firmware configuration.

Important values documented for the device family include:

| Value | Meaning |
|---:|---|
| `0xFF` | Access-port protection disabled for hardware-controlled devices |
| `0x5A` | Hardware disable selection for devices using the improved hardware/software protection scheme |
| `0x00` | Access-port protection enabled |

Do not select a value from this table without checking the current Product Specification, Revision 3 errata, MDK startup behavior, and selected SDK configuration.

For improved APPROTECT devices, startup software participates in completing the protection state. Use a current Nordic MDK/SDK and verify the generated startup implementation.

Before enabling APPROTECT:

1. verify SWD programming on unprotected devices;
2. verify the mass-erase/recover operation;
3. verify bootloader and firmware recovery;
4. program the complete UICR image;
5. reset and confirm protection;
6. confirm the production tool can recover a sample device;
7. only then enable the step for production units.

Access protection is not a substitute for secure boot, signed updates, rollback control, or key-management design.

## 14. BPROT — Flash Block Protection

BPROT prevents application code from writing or erasing selected flash pages.

Base address:

```text
BPROT = 0x40000000
```

Registers:

| Register | Offset | Protected regions |
|---|---:|---|
| `CONFIG0` | `0x600` | Flash pages 0–31 |
| `CONFIG1` | `0x604` | Flash pages 32–47 in bits 0–15 |
| `DISABLEINDEBUG` | `0x608` | Controls whether BPROT is disabled while debugging |

One protection bit maps to one 4 KB flash page:

```text
CONFIG0 bit n      → page n,       n = 0..31
CONFIG1 bit (n-32) → page n,       n = 32..47
```

Protection bits reset to zero. Firmware must set required protection bits early after every reset.

Writing `0` to a protection bit has no effect after it has been set. Protection is cleared only by reset.

If application code attempts to write or erase a protected page, the CPU HardFaults. If CPU code attempts `ERASEALL` while any block is protected, the operation is blocked and the CPU HardFaults.

Typical protected regions:

- bootloader pages;
- immutable vector/boot-selection pages;
- stack or platform code that must not be changed by application settings logic.

BPROT is runtime write/erase protection, not readout protection. APPROTECT controls external debug access.

## 15. Shared Peripheral Base Addresses

Some peripheral names are alternate views of the same base address and interrupt identity.

| ID | Base address | Alternate instances |
|---:|---:|---|
| 0 | `0x40000000` | `POWER`, `CLOCK`, `BPROT`, `APPROTECT` control area |
| 2 | `0x40002000` | `UART0`, `UARTE0` |
| 3 | `0x40003000` | `TWI0`, `TWIM0`, `TWIS0` |
| 4 | `0x40004000` | `SPI0`, `SPIM0`, `SPIS0` |
| 15 | `0x4000F000` | `CCM`, `AAR` |
| 20 | `0x40014000` | `EGU0`, `SWI0` |
| 21 | `0x40015000` | `EGU1`, `SWI1` |

Two alternate instances at one base address cannot be treated as independent drivers.

Required driver behavior:

- use one owner for each peripheral ID;
- disable the previous implementation before changing mode;
- clear stale events and interrupts;
- restore pin and EasyDMA configuration;
- prevent SDK and direct-register layers from owning the same block;
- document shared IRQ use.

## 16. Register Side Effects and Fault Cases

| Operation | Side effect or fault |
|---|---|
| Byte/halfword NVMC write | HardFault |
| EasyDMA pointer outside valid RAM | HardFault, RAM corruption, or invalid transfer |
| Write/erase protected BPROT page | HardFault |
| CPU `ERASEALL` while any BPROT page is protected | Operation blocked and HardFault |
| Incomplete partial erase | Page contents undefined |
| UICR changed without reset | New functional configuration not yet active |
| `PSELRESET[0] != PSELRESET[1]` | nRESET is not exposed on GPIO |
| Wrong APPROTECT sequence | Lost debug access or incomplete protection |
| NVMC mode left in write/erase | Later accidental memory modification becomes possible |
| Alternate peripheral initialized concurrently | Register, interrupt, and state conflict |

Every HardFault handler should preserve enough context to distinguish:

- invalid memory access;
- protected flash operation;
- misaligned NVMC write;
- invalid EasyDMA pointer;
- corrupted stack or return address.

## 17. Production Memory Image

A production memory release should contain:

```text
application / stack HEX
bootloader HEX, if used
settings / metadata HEX, if preprogrammed
UICR records
expected FICR compatibility rules
BPROT startup configuration
APPROTECT production policy
merged-image hash
programming and verification procedure
```

Recommended production checks:

1. Read `FICR.INFO.PART` and reject non-nRF52810 devices.
2. Read `INFO.VARIANT` and apply the correct errata policy.
3. Verify flash and RAM size fields.
4. Program and verify the merged flash image.
5. Program and verify the complete UICR image.
6. Reset and test nRESET mapping.
7. Run functional and RF tests.
8. Enable/confirm APPROTECT only at the controlled final stage.
9. Test recovery on sampled units using the released production tool.

Store the expected UICR values as source-controlled data. Do not let separate build scripts silently program different reset or protection values.

## 18. Recommended Firmware Architecture

```text
Application services
└── request persistent operations

Storage service
├── validates address and partition ownership
├── schedules erase/write around radio timing
├── provides CRC/versioned records
└── reports completion and wear state

Memory platform layer
├── selected SDK storage/NVMC API
├── BPROT policy
├── FICR identity access
├── UICR read/production verification
└── errata workarounds
```

The normal application API should not expose arbitrary address write or erase operations.

Suggested boundaries:

```c
typedef struct {
    uint32_t part;
    uint32_t variant;
    uint32_t package;
    uint32_t flash_kib;
    uint32_t ram_kib;
    uint64_t device_id;
} nrf52810_device_info_t;

bool platform_device_info_read(nrf52810_device_info_t *info);
bool storage_record_write(uint16_t record_id,
                          const void *data,
                          size_t length);
bool storage_record_read(uint16_t record_id,
                         void *data,
                         size_t capacity,
                         size_t *actual_length);
```

UICR programming, APPROTECT, mass erase, and arbitrary page erase should remain production-only or bootloader-owned operations.

## 19. Main Technical Risks

| Risk | Consequence | Required control |
|---|---|---|
| Linker layout assumes a larger nRF52 device | Image exceeds physical flash/RAM | Use nRF52810-specific target and inspect the map file. |
| Storage page overlaps stack or bootloader | Firmware or persistent data corruption | Maintain one authoritative partition definition. |
| Flash word is rewritten with a `0→1` transition | Verify failure or invalid data | Erase the page and use an append/copy-on-write storage design. |
| Misaligned NVMC write | HardFault | Validate address and length before enabling NVMC write mode. |
| UICR is used as a runtime settings store | Wear and destructive full-region erase | Use normal settings pages instead. |
| Incomplete UICR restore after erase | Lost reset/protection configuration | Program UICR from one versioned complete image. |
| BPROT configured too late | Boot pages writable during startup | Set protection immediately after reset. |
| APPROTECT enabled before recovery test | Device appears inaccessible | Validate the exact production recovery flow first. |
| FICR security roots are logged | Sensitive factory material exposure | Restrict access and never include roots in normal diagnostics. |
| Shared peripheral base is double-owned | Register and interrupt corruption | Centralize peripheral ID ownership. |

## 20. Implementation Checklist

- [ ] Confirm flash range `0x00000000–0x0002FFFF` and RAM range `0x20000000–0x20005FFF`.
- [ ] Confirm `CODEPAGESIZE × CODESIZE == 192 KB`.
- [ ] Use a page-aligned, source-controlled flash partition.
- [ ] Inspect the linker map for stack, settings, bootloader, and DFU overlap.
- [ ] Keep every EasyDMA buffer in valid RAM.
- [ ] Perform only aligned 32-bit NVMC writes.
- [ ] Check that each programmed word requires only `1→0` transitions.
- [ ] Poll `NVMC.READY`; do not use a fixed delay as the completion test.
- [ ] Return `NVMC.CONFIG` to read-only after every operation.
- [ ] Verify page erase and word write results.
- [ ] Do not use Nordic-reserved UICR words for application data.
- [ ] Treat the 32 customer UICR words as rarely changed configuration.
- [ ] Program identical values into both `PSELRESET` registers.
- [ ] Set BPROT immediately after reset for protected pages.
- [ ] Review improved APPROTECT behavior for the actual revision and MDK.
- [ ] Validate mass-erase/recovery before production protection.
- [ ] Keep one owner for every shared peripheral base address.
- [ ] Record FICR part/variant in production traceability data.
- [ ] Apply NVMC and APPROTECT errata for the actual silicon revision.

## 21. References

1. [nRF52810 Product Specification](https://docs.nordicsemi.com/r/bundle/ps_nrf52810/)
2. [nRF52810 Revision 3 Errata](https://docs.nordicsemi.com/bundle/errata_nRF52810_Rev3/page/ERR/nRF52810/Rev3/latest/err_810.html)
3. [Nordic nRF52810 documentation resources](https://docs.nordicsemi.com/r/bundle/additionalresources/page/additionalresources/nrf52-series/nrf52810)
4. [Nordic guidance for improved APPROTECT](https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/working-with-the-nrf52-series-improved-approtect)