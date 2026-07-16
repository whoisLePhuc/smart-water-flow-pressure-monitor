---
document_id: FW-IMPL-091
title: Build and Variant Strategy
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-HW-001
  - DEC-HW-004
  - DEC-HW-005
  - DEC-ARCH-008
---

# Build and Variant Strategy

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **MODULAR CMAKE IMPLEMENTED / VARIANT SYSTEM PARTIAL**
- Đã có: target theo layer/subsystem, driver MAX/ZSSC target riêng, test taxonomy và optional sanitizer helpers.
- Chưa có: generated product_config, board/product matrix, pinned CI toolchain và multi-variant release evidence.

## 1. Target graph policy

Mỗi subsystem build thành target rõ dependency. Domain không link platform/driver. Service link domain/infrastructure/ports cần thiết. App/composition link concrete services/adapters. Không compile cùng source vào nhiều target nếu tạo duplicate symbol/state.

## 2. Variant layers

| Layer | Chọn ở đâu | Ví dụ |
|---|---|---|
| Platform | CMake preset/toolchain | Linux, STM32 |
| Board | board target/generated config | pin/channel/peripheral |
| Product | variant manifest | sensor capability/profile |
| Device | persistent provisioning | calibration/fingerprint |
| Runtime | validated config | period/threshold/window |

## 3. Requirements

| ID | Requirement |
|---|---|
| FW-BUILD-REQ-001 | Target name/path MUST unique và phản ánh subsystem. |
| FW-BUILD-REQ-002 | Domain MUST không include/link platform HAL. |
| FW-BUILD-REQ-003 | Variant MUST selected tại composition/build boundary. |
| FW-BUILD-REQ-004 | Business logic MUST không chứa scattered board `#ifdef`. |
| FW-BUILD-REQ-005 | Open hardware decisions MUST không tạo release constant giả. |
| FW-BUILD-REQ-006 | Generated config MUST schema/version/source traceable. |
| FW-BUILD-REQ-007 | Warnings-as-errors và architecture check MUST là CI gate. |
| FW-BUILD-REQ-008 | Linux and STM32 MUST share domain/service/protocol source. |
| FW-BUILD-REQ-009 | Release MUST record compiler, flags, variant, commit và test evidence. |
| FW-BUILD-REQ-010 | OTA target không được thêm vào MVP theo `DEC-ARCH-008`. |

## 4. Build matrix mục tiêu

Linux debug + sanitizers/tests; Linux release simulation; STM32 debug; STM32 release; ít nhất hai product variants; static analysis. Hardware-specific variants chỉ release sau `DEC-HW-004/005` và datasheet/qualification gates tương ứng.

## 5. Verification

CMake configure from clean tree, dependency graph review, no umbrella/compatibility targets, two variants with different profile values, reproducible build metadata và size budget comparison.


