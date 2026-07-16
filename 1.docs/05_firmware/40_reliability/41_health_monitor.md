---
document_id: FW-REL-041
title: Health Monitor
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-ERR-001
  - DEC-ERR-003
  - DEC-ERR-004
  - DEC-MEAS-004
---

# Health Monitor

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PLANNED**
- Đã có nền tảng: PowerHealth, measurement statuses, queue/scheduler counters và FSM.
- Chưa có: dedicated HealthMonitor, policy/config, aggregate snapshot và escalation tests.

## 1. Responsibility

HealthMonitor tổng hợp freshness, repeated device/transport faults, queue pressure, storage integrity, power health và runtime progress. Nó không thay thế owner status và không tự gọi HAL/reset.

## 2. Input/output

Input là immutable snapshot, counters và typed health events. Output là orthogonal health flags, reason/evidence version và escalation/recovery event. Primary mode vẫn do FSM sở hữu.

## 3. Requirements

| ID | Requirement |
|---|---|
| FW-HLTH-REQ-001 | Health evaluation MUST deterministic với cùng input/config/time. |
| FW-HLTH-REQ-002 | Windows/deadlines MUST dùng monotonic time. |
| FW-HLTH-REQ-003 | Invalid/stale MUST khác fault-confirmed. |
| FW-HLTH-REQ-004 | Counter arithmetic MUST checked hoặc saturating. |
| FW-HLTH-REQ-005 | Escalation MUST qua event/FSM policy. |
| FW-HLTH-REQ-006 | Recovery clear MUST cần fresh functional evidence. |
| FW-HLTH-REQ-007 | Degraded-safe MUST tuân `DEC-ERR-003`. |
| FW-HLTH-REQ-008 | Health monitor state MUST instance-owned/resettable. |
| FW-HLTH-REQ-009 | Repeated watchdog history MUST theo `DEC-ERR-004`. |

## 4. Proposed ownership

AppComposition sở hữu HealthMonitor; services expose snapshots/counters; monitor evaluates trên scheduled/event boundary và publishes typed health view qua repository transaction hoặc dedicated immutable view.

## 5. Acceptance

Unit tests window/threshold/stale/recovery, integration sensor outage/queue pressure/storage corrupt/power transition, system escalation và reset recovery. Trước đó file này là normative design.


