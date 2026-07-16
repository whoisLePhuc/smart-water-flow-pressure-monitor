---
document_id: FW-PLAT-053
title: Interrupt DMA and Callback Rules
status: DRAFT
version: 1.0
owner: Firmware
last_updated: 2026-07-16
related_decisions:
  - DEC-HW-006
  - DEC-HW-007
  - DEC-DATA-003
---

# Interrupt, DMA and Callback Rules

## 0. Trạng thái triển khai tại firmware baseline

- Firmware baseline: `4044414a7610d53b24c10814c12eaa09864e949e`
- Implementation status: **NORMATIVE CONTRACT / PARTIAL IMPLEMENTATION**
- Đã có: bounded event model, Linux scheduled completion pattern và operation/generation fields ở nhiều driver/runtime types.
- Chưa có: STM32 IRQ/DMA routing implementation. Current STM32 ADC adapter is synchronous polling and does not demonstrate this callback pattern.

## 1. ISR rule

ISR/callback chỉ capture/clear hardware evidence tối thiểu, identify operation, store bounded completion và post/defer event. Không compute, parse large payload, encode storage/telemetry, transition FSM hoặc log chuỗi.

## 2. Ownership and race rules

| Resource | Owner/lifetime |
|---|---|
| HAL/peripheral handle | Platform adapter/composition |
| DMA buffer | Adapter đến terminal completion/cancel |
| Operation token | Driver/service; generation changes on reset/rebind |
| Event payload | Queue-owned bounded copy |
| Domain result | Service transaction, không ISR |

## 3. Requirements

| ID | Requirement |
|---|---|
| FW-IRQ-REQ-001 | ISR work MUST bounded và non-blocking. |
| FW-IRQ-REQ-002 | Callback MUST validate handle/channel/token. |
| FW-IRQ-REQ-003 | Late/duplicate completion MUST not terminate new operation. |
| FW-IRQ-REQ-004 | DMA buffer MUST not reuse before terminal state. |
| FW-IRQ-REQ-005 | Shared ISR/thread fields MUST atomic hoặc protected by proven critical section. |
| FW-IRQ-REQ-006 | Queue full MUST have counter/escalation policy. |
| FW-IRQ-REQ-007 | Timeout and completion race MUST resolve exactly once. |
| FW-IRQ-REQ-008 | Bus recovery MUST increment generation for affected clients. |
| FW-IRQ-REQ-009 | Callback MUST not publish RuntimeSnapshot directly. |
| FW-IRQ-REQ-010 | Priority/critical-section WCET MUST be analyzed on target. |

## 4. Event sequence

Start operation in event context → configure DMA/IRQ → return → ISR captures terminal evidence → event queue → driver validates token/generation → service parses/computes → repository transaction commits.

## 5. Verification

Late/duplicate callback, timeout same tick, queue full, interrupt storm, cancel/rebind, DMA reuse and shared-I2C recovery tests. STM32 HIL required; Linux action queue only validates logical order.


