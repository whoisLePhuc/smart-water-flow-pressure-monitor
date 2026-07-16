---
document_id: FW-REL-043
title: Low Power Mode
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-HW-007
  - DEC-PWR-001
  - DEC-PWR-002
---

# Low Power Mode

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PARTIAL LOGIC / PLANNED HARDWARE**
- Đã có: FSM/mode guard và PowerService/PowerFacade foundation.
- Chưa có: STM32 STOP2 entry/wake, peripheral quiesce/rebind, clock restoration và HIL.
- Battery thresholds/hysteresis còn phụ thuộc OPEN `DEC-PWR-001`; không hard-code release limits.

## 1. Target sequence

FSM request → guard → stop/admit operations → checkpoint theo policy → platform enter STOP2 → allowed wake source → restore clocks/timebase → rebind peripheral → reject stale completions → fresh readiness evidence → resume mode.

## 2. Requirements

| ID | Requirement |
|---|---|
| FW-LP-REQ-001 | Only FSM/policy MAY authorize low-power entry. |
| FW-LP-REQ-002 | Critical storage/config transaction MUST complete hoặc rollback trước sleep. |
| FW-LP-REQ-003 | Wake sources MUST theo `DEC-HW-007`. |
| FW-LP-REQ-004 | Monotonic deadline semantics across sleep MUST documented/tested. |
| FW-LP-REQ-005 | Pre-sleep completion MUST không apply vào post-wake generation. |
| FW-LP-REQ-006 | Peripheral resume MUST require functional verification. |
| FW-LP-REQ-007 | Power-critical MUST tuân reset/brownout-only `DEC-PWR-002`. |
| FW-LP-REQ-008 | Threshold MUST đến từ validated profile/config. |
| FW-LP-REQ-009 | Sleep preparation MUST bounded; không wait vô hạn. |

## 3. Current code truth

Không có STM32 STOP2 implementation. ADC adapter hiện polling synchronous; cần quyết định quiesce/cancel trước khi dùng low power. Linux simulation chỉ có logic/time foundation, không chứng minh clock/peripheral target behavior.

## 4. Verification

State-transition tests, storage/config busy, wake by RTC/MAX INT/LPUART1, clock continuity, stale callback, repeated sleep/wake và power fault HIL.


