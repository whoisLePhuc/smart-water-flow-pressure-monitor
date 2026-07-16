---
document_id: FW-REL-044
title: Boot and Self Check
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-ERR-003
  - DEC-ERR-004
  - DEC-DATA-004
  - DEC-MODE-001
---

# Boot and Self Check

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PARTIAL**
- Đã có: AppComposition initialization, FSM init, repository init, scheduler/jobs và storage boot-restore tests.
- Chưa có: reset-reason port, full persistent/config restore in composition, device probes, self-check result aggregation và target startup.

## 1. Boot sequence contract

Minimal platform → capture reset reason → clocks/event infrastructure → validate/load persistent record/config → initialize repository/services → probe critical devices → register handlers/jobs → evaluate readiness → FSM enters NORMAL/degraded/ERROR. Limited BLE in INIT chỉ sau minimal readiness theo `DEC-MODE-001`.

## 2. Requirements

| ID | Requirement |
|---|---|
| FW-BOOT-REQ-001 | Every reset MUST begin canonical mode INIT. |
| FW-BOOT-REQ-002 | Reset reason MUST captured before destructive clear. |
| FW-BOOT-REQ-003 | Unverified persistent record MUST không become active. |
| FW-BOOT-REQ-004 | Corrupt slot MUST use validated A/B recovery hoặc explicit safe default. |
| FW-BOOT-REQ-005 | Device probe failure MUST bounded và observable. |
| FW-BOOT-REQ-006 | Measurement publication MUST wait for required profile/repository readiness. |
| FW-BOOT-REQ-007 | Degraded entry MUST prove core readiness per `DEC-ERR-003`. |
| FW-BOOT-REQ-008 | Watchdog repeated-reset policy MUST gate auto-normal. |
| FW-BOOT-REQ-009 | Self-check output MUST immutable/diagnosable. |
| FW-BOOT-REQ-010 | Boot MUST không phụ thuộc emergency flush trước reset. |

## 3. Current mapping

`src/app/app_composition.*`, `system_fsm.*`, repository/storage services và `test_boot_restore.c`. AppComposition hiện không sở hữu concrete StorageService/config restore; storage facade còn thin.

## 4. Verification

Cold boot, empty/corrupt/newest A-B slot, watchdog reset, peripheral unavailable, config incompatible, limited INIT access và recovery-to-normal tests; STM32 startup/HIL required.


