---
document_id: FW-IMPL-090
title: Firmware Implementation Plan
status: ACTIVE
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-ARCH-005
  - DEC-ARCH-007
  - DEC-HW-006
  - DEC-HW-007
  - DEC-ERR-004
---

# Firmware Implementation Plan

## 0. Baseline và mục tiêu

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Kiến trúc source tree đã refactor và architecture check pass.
- Mục tiêu tiếp theo là hoàn thiện vertical slices mà không phá dependency/ownership hiện tại.
- Plan này không tuyên bố feature hoàn thành; completion được cập nhật tại [Firmware Traceability](95_firmware_traceability.md).

## Phase 0 — Reproducible baseline

- Cài/pin CMake, compiler, CTest và sanitizer toolchain.
- Clean configure/build, chạy toàn bộ unit/contract/integration/system.
- Lưu test inventory, binary size, stack/static memory baseline.
- Enforce architecture check và không compatibility target/header ngoài allowlist.

Exit: CI tái lập được trên commit sạch; mọi failure được triage.

## Phase 1 — Production measurement compute binding

- Tạo concrete MAX raw-result và ZSSC raw-result services có `MeasurementService.compute`.
- Đăng ký raw-ready/I2C completion events.
- Nối FlowService, PressureService, CalibrationService qua shared `RepoWriteTxn`.
- Bảo đảm một source event publish tối đa một snapshot.
- Thêm generation/correlation và raw payload validation.

Exit: Linux end-to-end raw peer → driver → compute → snapshot; invalid/timeout/duplicate tests.

## Phase 2 — Power/ADC target vertical slice

- Đo WCET của synchronous ADC polling hiện tại.
- Quyết định giữ bounded polling hay refactor async/DMA; tài liệu ưu tiên async để phù hợp runtime.
- Board HAL/channel/reference calibration binding.
- PowerConfig validation; không chốt threshold khi `DEC-PWR-001` còn OPEN.
- Snapshot + status-changed event + telemetry mapping.

Exit: cross-build, target contract/HIL normal-timeout-error và event-loop latency budget.

## Phase 3 — Storage boundary closure

- Chọn canonical dependency: StorageService dùng StoragePort/interface injected thay vì direct FramDriver, hoặc ghi quyết định ngoại lệ có lý do.
- AppComposition sở hữu StorageService/FramDriver/port context.
- STM32 shared-I2C binding theo `DEC-HW-006`.
- Boot restore/checkpoint/power-loss end-to-end.

Exit: không còn unused parallel storage path; Linux và STM32 dùng cùng service/codec semantics.

## Phase 4 — Configuration lifecycle

PendingConfig repository → schema/semantic validation → A/B persist/verify → safe-boundary apply → per-service acknowledgement → ActiveConfig version. Bổ sung multi-variant build/test và rollback/power-loss.

## Phase 5 — Reliability and target runtime

Health monitor, structured error registry, diagnostic policy sau `DEC-DIAG-001`, watchdog progress gate/reset reason, boot/self-check, STOP2/wake/resume theo `DEC-HW-007`.

## Phase 6 — Product interfaces

Theo thứ tự release: modem/telemetry transport → BLE/config → LCD sau `DEC-HW-004` → factory/service interface. Mỗi callback đi qua port/event boundary.

## Quality gates mỗi phase

- Ownership và public API documented.
- Không mutable static runtime state.
- No blocking ngoài bounded/approved platform call.
- Unit + contract + integration + system regression.
- Requirement/decision/source/test traceability cập nhật cùng code.
- Planned → Implemented chỉ khi composition binding và acceptance test tồn tại.


