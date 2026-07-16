---
document_id: FW-REL-040
title: Error Detection and Recovery
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-ERR-001
  - DEC-ERR-002
  - DEC-ERR-003
  - DEC-ERR-005
---

# Error Detection and Recovery

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **PARTIAL**
- Đã có: port/domain statuses, checked math, event counters, scheduler generation, repository abort, storage A/B recovery và fault-oriented tests.
- Chưa có: unified error registry, health aggregation, complete escalation policy, watchdog/diagnostic persistence.

## 1. Fault domains

Transport/port, device, numeric/validation, queue/resource, storage integrity, configuration và internal invariant phải giữ nguyên source context. Service map lỗi thấp tầng sang domain outcome; không trả raw HAL code cho product consumer.

## 2. Recovery ladder

Detect → isolate operation/resource → bounded retry/re-init theo `DEC-ERR-001` → resource/system recovery theo `DEC-ERR-002` → degraded-safe chỉ khi chứng minh readiness theo `DEC-ERR-003` → ERROR/reset policy.

## 3. Requirements

| ID | Requirement |
|---|---|
| FW-ERR-REQ-001 | Retry MUST bounded, configured và chỉ cho operation idempotent. |
| FW-ERR-REQ-002 | Timeout/late/duplicate completion MUST terminate operation tối đa một lần. |
| FW-ERR-REQ-003 | Recovery MUST dùng correlation/generation. |
| FW-ERR-REQ-004 | Repository writer failure MUST abort transaction. |
| FW-ERR-REQ-005 | Storage corruption MUST không publish unverified record. |
| FW-ERR-REQ-006 | Queue overflow MUST observable và có explicit policy. |
| FW-ERR-REQ-007 | Driver MUST không tự reset system/FSM. |
| FW-ERR-REQ-008 | Internal invariant MUST theo structured policy `DEC-ERR-005`. |
| FW-ERR-REQ-009 | Infinite retry/recovery loop MUST NOT xảy ra. |
| FW-ERR-REQ-010 | Recovery success MUST cần functional verification. |

## 4. Current mapping và tests

`domain/common/status.h`, `ports/port_status.h`, checked math, event queue, scheduler, repo transaction, storage codec/A-B. Tests cover invalid numeric, queue/resource boundaries, duplicate volume, corrupt/power-loss storage và simulation faults.

## 5. Gap/acceptance

Cần error registry, per-fault config, escalation event binding, health monitor và target fault injection. Status chỉ chuyển Implemented khi normal/degraded/error transitions được test end-to-end.


