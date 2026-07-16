---
document_id: FW-REL-042
title: Watchdog Strategy
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-ERR-004
  - DEC-ERR-005
---

# Watchdog Strategy

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PLANNED**
- Đã có nền tảng: bounded cooperative runtime, scheduler/event-loop tests.
- Chưa có: watchdog port, Linux fake, STM32 adapter, progress gate, reset reason binding.

## 1. Strategy

Watchdog refresh chỉ khi chứng minh event loop và critical service progress. Không refresh vô điều kiện từ timer ISR. Reset luôn boot qua INIT; repeated reset window/threshold theo `DEC-ERR-004`.

## 2. Requirements

| ID | Requirement |
|---|---|
| FW-WDG-REQ-001 | Refresh MUST gated bởi progress evidence. |
| FW-WDG-REQ-002 | Watchdog ISR/timer MUST không tự refresh vô điều kiện. |
| FW-WDG-REQ-003 | Event loop heartbeat MUST không đủ nếu critical operation stuck. |
| FW-WDG-REQ-004 | Reset reason MUST captured trước khi clear. |
| FW-WDG-REQ-005 | Repeated reset threshold MUST configurable và chặn auto-normal. |
| FW-WDG-REQ-006 | Long operation MUST asynchronous/bounded. |
| FW-WDG-REQ-007 | Production assertion MUST không treo vô hạn. |
| FW-WDG-REQ-008 | Linux fake MUST mô phỏng expiry deterministically. |

## 3. Port boundary

`WatchdogPort`: init/start/refresh/get_reset_reason. Health/progress gate thuộc app/service; platform adapter chỉ thao tác peripheral. Static lifetime của adapter do composition sở hữu.

## 4. Verification

Fake clock expiry, missing event progress, storage busy bounded, false refresh prevention, watchdog reset boot, repeated-reset safe path và STM32 timing/HIL. Không tăng watchdog timeout để che blocking ADC/modem operations.


