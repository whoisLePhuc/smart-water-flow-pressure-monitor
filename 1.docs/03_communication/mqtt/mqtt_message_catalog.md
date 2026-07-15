# MQTT Message Catalog

| Metadata | Value |
| --- | --- |
| Document ID | COMM-MQTT-MSG-001 |
| Status | Proposed |
| Baseline | MQTT application messages mapped from the common data contract |
| Applies to | STM32 firmware, MQTT adapter, EC200U integration, broker/backend services, and Linux simulation |

## 1. Purpose

This document catalogs every proposed MQTT application message used by the Smart Water Flow and Pressure Monitor. For each message, it defines the topic mapping, direction, trigger, payload representation, QoS, retained behavior, duplicate handling, expiry, and acceptance rule.

The semantic meaning, units, valid states, and logical field types are owned by [../01_common_data_contract.md](../01_common_data_contract.md). This document maps those objects to MQTT; it MUST NOT create a second independent data model.

## 2. Scope

This document covers:

- outbound common-record messages;
- inbound command and configuration requests;
- command and configuration results;
- optional application receipts;
- Last Will/status mapping;
- JSON representation proposed for the MVP;
- field, size, duplicate, expiry, and acknowledgement behavior.

It does not define:

- measurement algorithms or event-code meanings;
- MQTT connection/session lifecycle;
- topic hierarchy or broker ACL implementation;
- HTTP payloads or endpoints;
- the final approved command/configuration allowlists;
- production credentials or broker deployment.

## 3. Normative language

The terms **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** express requirement strength.

Requirements marked **Proposed** are the integration baseline until payload encoding, size limits, acknowledgement rules, and enabled remote operations are verified and approved.

## 4. Message catalog summary

Topic prefix used below:

```text
{root} = swfpm/{environment}/v{mqtt_contract_major}/devices/{device_id}
```

| ID | Message | Topic | Direction | QoS | Retain | Application receipt |
| --- | --- | --- | --- | ---: | ---: | --- |
| `MQTT-MSG-001` | Telemetry | `{root}/up/telemetry` | Device → service | 1 | No | Proposed optional |
| `MQTT-MSG-002` | Event | `{root}/up/events` | Device → service | 1 | No | Proposed required for high/critical events |
| `MQTT-MSG-003` | Device status | `{root}/up/status` | Device → service | 1 | Conditional | Proposed optional |
| `MQTT-MSG-004` | Diagnostic | `{root}/up/diagnostics` | Device → service | 1 | No | No by default |
| `MQTT-MSG-005` | Command request | `{root}/down/commands` | Service → device | 1 | No | Command result, not record receipt |
| `MQTT-MSG-006` | Command result | `{root}/up/command-results` | Device → service | 1 | No | Proposed required |
| `MQTT-MSG-007` | Configuration request | `{root}/down/configurations` | Service → device | 1 | No | Configuration result |
| `MQTT-MSG-008` | Configuration result | `{root}/up/configuration-results` | Device → service | 1 | No | Proposed required |
| `MQTT-MSG-009` | Application receipt | `{root}/down/receipts` | Service → device | 1 | No | Not applicable |

Final receipt requirements remain a project decision. Until approved, implementations MUST distinguish MQTT QoS acknowledgement from remote application acceptance.

## 5. Proposed MQTT payload profile

### 5.1 Encoding

The proposed MVP representation is UTF-8 JSON.

| Property | Proposed rule |
| --- | --- |
| Content | One JSON object per MQTT PUBLISH |
| Character encoding | UTF-8 |
| Object member names | `snake_case` |
| Unknown JSON members | Ignore only when allowed by versioning policy; never reinterpret |
| Duplicate JSON member names | Reject |
| Non-finite numbers | Prohibited |
| Comments/trailing commas | Prohibited |
| Whitespace | Allowed; producers SHOULD emit compact JSON |
| Compression | Not used in MVP |
| Encryption inside payload | Not used by default; TLS protects transport |

CBOR or another encoding MAY be introduced later only with an explicit content/encoding discriminator, deterministic test vectors, size limits, and compatibility plan.

### 5.2 Integer representation

JSON numbers cannot safely preserve every 64-bit integer in common consumer runtimes. Therefore:

- every `uint64` and `int64` field MUST be encoded as a base-10 JSON string in the proposed MVP;
- `uint32`, `int32`, `uint16`, `int16`, enums, and flags are encoded as JSON integers;
- decimal strings MUST contain no sign for unsigned values, no leading `+`, no whitespace, and no leading zeros except the value `"0"`;
- consumers MUST validate the logical range before conversion;
- producers MUST NOT encode the same logical field sometimes as a number and sometimes as a string.

Example:

```json
{
  "record_id": "4294967297",
  "flow_rate_ml_per_h": "-1250",
  "pressure_pa": 315000
}
```

### 5.3 Enum and flag representation

- enumerations are encoded using their stable numeric codes;
- bit flags are encoded as unsigned JSON integers;
- producers MUST set reserved flag bits to zero;
- consumers MUST follow the unknown-enum/unknown-flag rules in [../02_protocol_versioning.md](../02_protocol_versioning.md).

Symbolic enum names MAY appear in logs and documentation, but are not additional wire fields unless a message definition explicitly includes them.

### 5.4 Optional, unavailable, and null

- an optional field that is not applicable MUST be omitted;
- an unavailable measurement value MUST be omitted while its quality metadata indicates unavailable;
- numeric zero MUST remain a valid numeric value;
- JSON `null` MUST NOT be used as a generic substitute for unavailable data;
- receivers MUST NOT invent a default semantic value for an omitted required field.

## 6. Common outbound envelope

Every device-originated record except the broker-generated Last Will maps one `CommonRecordEnvelope` to one MQTT payload:

```json
{
  "schema_version": 1,
  "record_type": 1,
  "record_id": "4294967297",
  "device_id": "WM-000001",
  "created_at_unix_ms": "1784102400000",
  "uptime_ms": "86400000",
  "time_quality": 2,
  "priority": 1,
  "expires_after_ms": 86400000,
  "payload": {}
}
```

### 6.1 Envelope validation

The receiver MUST validate:

1. required fields and logical integer ranges;
2. supported `schema_version` and `record_type`;
3. topic `device_id` equals payload `device_id` and authenticated device identity;
4. `record_type` is permitted on the selected topic;
5. `record_id` is non-zero;
6. payload object matches the selected record type;
7. encoded payload is within configured limits;
8. record has not expired according to accepted time-quality rules.

The immutable deduplication key is `(device_id, record_id)`. Receiving that key again with identical immutable content is a retry duplicate. Receiving it with different immutable content is a contract violation.

## 7. Common measurement object mapping

Measurement objects use the following JSON shape:

```json
{
  "value": 315000,
  "quality_flags": 1,
  "age_ms": 250,
  "sampled_at_unix_ms": "1784102399750"
}
```

The `value` uses the logical quantity type. For 64-bit quantities it is a decimal string; for 32-bit or smaller quantities it is a JSON integer.

The field may be omitted when unavailable:

```json
{
  "quality_flags": 2,
  "age_ms": 4294967295
}
```

`quality_flags` and `age_ms` remain required. `sampled_at_unix_ms` is optional when no wall-clock sample time is available.

## 8. MQTT-MSG-001 — Telemetry

| Property | Value |
| --- | --- |
| Topic | `{root}/up/telemetry` |
| Direction | Device → ingestion service |
| Common type | `RECORD_TELEMETRY` / `TelemetryRecord` |
| Trigger | Scheduled snapshot, policy-requested delivery, or retry of an existing record |
| QoS | 1 |
| Retained | No |
| Duplicate key | `(device_id, record_id)` |
| Expiry | `created_at_unix_ms` + `expires_after_ms` subject to time-quality policy |
| Completion | Proposed: QoS acknowledgement gives `DELIVERY_TRANSPORT_ACK_ONLY`; application receipt gives `DELIVERY_ACCEPTED` |

### 8.1 Payload fields

The envelope is defined in Section 6. `payload` contains exactly the logical `TelemetryRecord` fields from the common data contract:

| Field | JSON type | Required |
| --- | --- | ---: |
| `snapshot_sequence` | decimal string (`uint64`) | Yes |
| `snapshot_quality_flags` | integer (`flags32`) | Yes |
| `temperature` | measurement object with 32-bit value | Yes |
| `flow_rate` | measurement object with 64-bit decimal-string value | Yes |
| `pressure` | measurement object with 32-bit value | Yes |
| `total_forward_volume_ml` | decimal string (`uint64`) | Yes |
| `total_reverse_volume_ml` | decimal string (`uint64`) | Optional |
| `net_volume_ml` | decimal string (`int64`) | Optional |
| `leak_state` | integer enum | Yes |
| `leak_flags` | integer (`flags32`) | Yes |
| `battery_mv` | integer (`uint16`) | Optional |
| `device_health` | integer enum | Yes |
| `config_revision` | integer (`uint32`) | Yes |
| `calibration_revision` | integer (`uint32`) | Yes |

### 8.2 Example

```json
{
  "schema_version": 1,
  "record_type": 1,
  "record_id": "4294967297",
  "device_id": "WM-000001",
  "created_at_unix_ms": "1784102400000",
  "uptime_ms": "86400000",
  "time_quality": 2,
  "priority": 1,
  "expires_after_ms": 86400000,
  "payload": {
    "snapshot_sequence": "10025",
    "snapshot_quality_flags": 1,
    "temperature": {
      "value": 25375,
      "quality_flags": 1,
      "age_ms": 250,
      "sampled_at_unix_ms": "1784102399750"
    },
    "flow_rate": {
      "value": "125000",
      "quality_flags": 1,
      "age_ms": 250,
      "sampled_at_unix_ms": "1784102399750"
    },
    "pressure": {
      "value": 315000,
      "quality_flags": 1,
      "age_ms": 250,
      "sampled_at_unix_ms": "1784102399750"
    },
    "total_forward_volume_ml": "987654321",
    "total_reverse_volume_ml": "1200",
    "net_volume_ml": "987653121",
    "leak_state": 1,
    "leak_flags": 0,
    "battery_mv": 3710,
    "device_health": 1,
    "config_revision": 7,
    "calibration_revision": 3
  }
}
```

A retry MUST reuse the exact record identity and immutable semantic payload. It MUST NOT take a new snapshot while retaining the old `record_id`.

## 9. MQTT-MSG-002 — Event

| Property | Value |
| --- | --- |
| Topic | `{root}/up/events` |
| Direction | Device → event service |
| Common type | `RECORD_EVENT` / `EventRecord` |
| Trigger | Event raised, updated, cleared, or explicitly acknowledged by domain policy |
| QoS | 1 |
| Retained | No |
| Duplicate key | `(device_id, record_id)` |
| Completion | Proposed: application receipt required for `PRIORITY_HIGH` and `PRIORITY_CRITICAL`; configurable for lower priority |

### 9.1 Payload fields

`payload` contains:

- `event_code`, `event_class`, `severity`, `state`, and `occurrence_count`;
- conditional `first_seen_unix_ms` and `last_seen_unix_ms` as decimal strings;
- optional `related_snapshot_sequence` as a decimal string;
- optional bounded typed `context` from the owning event catalog.

### 9.2 Example

```json
{
  "schema_version": 1,
  "record_type": 2,
  "record_id": "4294967298",
  "device_id": "WM-000001",
  "created_at_unix_ms": "1784102405000",
  "uptime_ms": "86405000",
  "time_quality": 2,
  "priority": 2,
  "expires_after_ms": 604800000,
  "payload": {
    "event_code": 1001,
    "event_class": 2,
    "severity": 2,
    "state": 0,
    "occurrence_count": 1,
    "first_seen_unix_ms": "1784102405000",
    "last_seen_unix_ms": "1784102405000",
    "related_snapshot_sequence": "10026",
    "context": {
      "leak_state": 2
    }
  }
}
```

Numeric enum assignments for `EventClass`, `Severity`, and `EventState` MUST be frozen in the common/event catalog before implementation. The example codes are test candidates, not a substitute for that catalog.

## 10. MQTT-MSG-003 — Device status

| Property | Value |
| --- | --- |
| Topic | `{root}/up/status` |
| Direction | Device or broker Last Will → status service |
| Common type | `RECORD_DEVICE_STATUS` / `DeviceStatusRecord` for normal status |
| Trigger | Startup readiness, significant status change, periodic health report, planned disconnect, or Last Will |
| QoS | 1 |
| Retained | Proposed disabled for MVP; MAY be enabled only with stale-state policy |
| Duplicate key | Normal status: `(device_id, record_id)` |
| Completion | QoS acknowledgement normally sufficient unless product audit requires receipt |

### 10.1 Normal status payload

Normal device-originated status uses the common outbound envelope. `payload` maps all fields from `DeviceStatusRecord`, including health, operating/reset state, queue depth/capacity, channel states, optional battery/RSSI, and active configuration/calibration revisions.

```json
{
  "schema_version": 1,
  "record_type": 3,
  "record_id": "4294967299",
  "device_id": "WM-000001",
  "created_at_unix_ms": "1784102410000",
  "uptime_ms": "86410000",
  "time_quality": 2,
  "priority": 1,
  "expires_after_ms": 300000,
  "payload": {
    "device_health": 1,
    "operating_state": 3,
    "uptime_ms": "86410000",
    "reset_reason": 1,
    "active_fault_count": 0,
    "delivery_queue_depth": 2,
    "delivery_queue_capacity": 128,
    "preferred_channel": 3,
    "active_channel": 1,
    "mqtt_state": 4,
    "http_state": 2,
    "cellular_state": 4,
    "ble_state": 2,
    "battery_mv": 3700,
    "signal_rssi_dbm": -73,
    "config_revision": 7,
    "calibration_revision": 3
  }
}
```

### 10.2 Last Will payload

The broker publishes a preconfigured Last Will after an ungraceful MQTT disconnect. Because the broker cannot create a new persistent device `record_id` or capture current runtime state, the Last Will MUST NOT impersonate a newly sampled `DeviceStatusRecord`.

Proposed minimal Last Will object:

```json
{
  "schema_version": 1,
  "message_type": "mqtt_presence",
  "device_id": "WM-000001",
  "connection_state": "offline_unexpected",
  "session_started_at_unix_ms": "1784102300000",
  "time_quality": 2
}
```

Consumers MUST treat this as session evidence, not proof of device power loss, hardware failure, or data loss. The final Last Will schema and retained behavior remain a validation gate.

## 11. MQTT-MSG-004 — Diagnostic

| Property | Value |
| --- | --- |
| Topic | `{root}/up/diagnostics` |
| Direction | Device → diagnostic service |
| Common type | `RECORD_DIAGNOSTIC` / `DiagnosticRecord` |
| Trigger | Allowlisted diagnostic condition and rate/coalescing policy |
| QoS | 1; QoS 0 MAY be approved for explicitly lossy diagnostics |
| Retained | No |
| Duplicate key | `(device_id, record_id)` |
| Completion | MQTT QoS acknowledgement sufficient by default |

`payload` maps `diagnostic_code`, `severity`, `subsystem`, `occurrence_count`, conditional time fields, optional `vendor_code`, and up to four `context_u32` values.

```json
{
  "schema_version": 1,
  "record_type": 4,
  "record_id": "4294967300",
  "device_id": "WM-000001",
  "created_at_unix_ms": "1784102420000",
  "uptime_ms": "86420000",
  "time_quality": 2,
  "priority": 0,
  "expires_after_ms": 86400000,
  "payload": {
    "diagnostic_code": 3001,
    "severity": 1,
    "subsystem": 11,
    "occurrence_count": 3,
    "first_seen_unix_ms": "1784102350000",
    "last_seen_unix_ms": "1784102420000",
    "vendor_code": 42,
    "context_u32": [1, 60000]
  }
}
```

Diagnostic payloads MUST NOT contain credentials, raw AT transcripts, unrestricted logs, stack dumps, arbitrary memory, or free-form personal data.

## 12. MQTT-MSG-005 — Command request

| Property | Value |
| --- | --- |
| Topic | `{root}/down/commands` |
| Direction | Authorized command service → device |
| Common type | `CommandRequest` |
| Trigger | Explicit authorized remote action |
| QoS | 1 |
| Retained | No |
| Duplicate key | Authenticated originator scope plus `command_id` |
| Expiry | `expires_at_unix_ms`; an expiration mechanism is mandatory before enabling state-changing commands |
| Response | One or more `CommandResult` messages correlated by `command_id` |

### 12.1 Payload

```json
{
  "schema_version": 1,
  "command_id": "900000001",
  "command_type": 1,
  "issued_at_unix_ms": "1784102500000",
  "expires_at_unix_ms": "1784102560000",
  "requested_by": "ops-service",
  "parameters": {}
}
```

`command_id`, time fields, and any 64-bit parameter use decimal strings. `command_type` and parameters MUST come from an approved command catalog.

### 12.2 Processing sequence

Before executing a command, the device MUST:

1. confirm the message arrived on its exact authorized command topic;
2. enforce payload-size and JSON syntax limits before allocation;
3. validate schema version, required fields, types, ranges, and allowed parameters;
4. validate expiry/freshness using accepted time-quality policy;
5. enforce command-type authorization and product-state preconditions;
6. deduplicate by originator scope and `command_id`;
7. persist replay protection when repeated execution could be unsafe;
8. dispatch through the controlled command service rather than directly mutating repositories or hardware;
9. publish a correlated command result.

An MQTT acknowledgement confirms delivery to the subscribed client session; it does not mean the command succeeded.

Unsupported, expired, invalid, unauthorized, and conflicting commands MUST NOT execute. They SHOULD produce a bounded result when doing so does not disclose sensitive information.

No production command may provide arbitrary executable text, shell access, unrestricted memory/register access, or an unbounded vendor-command pass-through.

## 13. MQTT-MSG-006 — Command result

| Property | Value |
| --- | --- |
| Topic | `{root}/up/command-results` |
| Direction | Device → command service |
| Common type | `RECORD_COMMAND_RESULT` / `CommandResult` |
| Trigger | Command accepted for deferred processing or reaches a final outcome |
| QoS | 1 |
| Retained | No |
| Duplicate key | Outbound `(device_id, record_id)`; correlation uses `command_id` |
| Completion | Proposed application receipt required before deleting a persistent result |

The result uses the common outbound envelope. The command correlation identity is inside `payload`.

```json
{
  "schema_version": 1,
  "record_type": 5,
  "record_id": "4294967301",
  "device_id": "WM-000001",
  "created_at_unix_ms": "1784102501500",
  "uptime_ms": "86501500",
  "time_quality": 2,
  "priority": 2,
  "expires_after_ms": 604800000,
  "payload": {
    "command_id": "900000001",
    "command_type": 1,
    "result_code": 1,
    "completed_at_unix_ms": "1784102501500",
    "applied_revision": 8
  }
}
```

If an accepted intermediate result is published, a later final result uses a new outbound `record_id` but the same `command_id`. The backend MUST not confuse acceptance with final success.

## 14. MQTT-MSG-007 — Configuration request

| Property | Value |
| --- | --- |
| Topic | `{root}/down/configurations` |
| Direction | Authorized configuration service → device |
| Common type | Request envelope carrying `ConfigurationChangeSet` |
| Trigger | Explicit authorized configuration update or validation request |
| QoS | 1 |
| Retained | No |
| Duplicate key | Authenticated originator scope plus `request_id` |
| Response | `ConfigurationResult` correlated by `request_id` |

Proposed request shape:

```json
{
  "schema_version": 1,
  "request_id": "910000001",
  "issued_at_unix_ms": "1784102600000",
  "expires_at_unix_ms": "1784102660000",
  "requested_by": "config-service",
  "change_set": {
    "base_revision": 7,
    "change_count": 1,
    "changes": [
      {
        "key": "telemetry_interval_s",
        "value": 900
      }
    ],
    "apply_mode": 0
  }
}
```

The configuration catalog owns allowed keys, value types, ranges, and cross-field rules. The receiver MUST reject unknown keys, revision conflicts, invalid combinations, and protected credential changes outside their provisioning contract.

The exact generic representation of typed `value` remains an open decision; production implementation MUST NOT rely on this example until that representation is approved.

## 15. MQTT-MSG-008 — Configuration result

| Property | Value |
| --- | --- |
| Topic | `{root}/up/configuration-results` |
| Direction | Device → configuration service |
| Common type | `RECORD_CONFIGURATION_RESULT` / `ConfigurationResult` |
| Trigger | Configuration validation, commit, or apply outcome |
| QoS | 1 |
| Retained | No |
| Duplicate key | Outbound `(device_id, record_id)`; correlation uses `request_id` |
| Completion | Proposed application receipt required before deleting a persistent result |

```json
{
  "schema_version": 1,
  "record_type": 6,
  "record_id": "4294967302",
  "device_id": "WM-000001",
  "created_at_unix_ms": "1784102601500",
  "uptime_ms": "86601500",
  "time_quality": 2,
  "priority": 2,
  "expires_after_ms": 604800000,
  "payload": {
    "request_id": "910000001",
    "result_code": 1,
    "previous_revision": 7,
    "new_revision": 8,
    "apply_state": 1,
    "field_errors": []
  }
}
```

The result MUST NOT echo protected credential values. Per-field errors MUST use stable bounded codes and keys from the configuration catalog, not unrestricted diagnostic text.

## 16. MQTT-MSG-009 — Application receipt

| Property | Value |
| --- | --- |
| Topic | `{root}/down/receipts` |
| Direction | Authorized ingestion service → device |
| Common type | MQTT-specific application acceptance object |
| Trigger | Backend completes validation/deduplication for a record requiring a receipt |
| QoS | 1 |
| Retained | No |
| Duplicate key | `(device_id, record_id, receipt_status)` |
| Expiry | Backend and device MUST bound receipt relevance |

Proposed payload:

```json
{
  "schema_version": 1,
  "device_id": "WM-000001",
  "record_id": "4294967301",
  "receipt_status": 0,
  "received_at_unix_ms": "1784102501800",
  "remote_receipt_id": "rcpt-01J2ABCDEF",
  "error_code": 0,
  "retry_after_ms": 0
}
```

### 16.1 Proposed receipt status

| Code | Name | Device interpretation |
| ---: | --- | --- |
| 0 | `RECEIPT_ACCEPTED` | Remote application accepted or already accepted the immutable record |
| 1 | `RECEIPT_RETRYABLE` | Keep the same record and retry no earlier than policy allows |
| 2 | `RECEIPT_REJECTED` | Permanent schema/policy rejection; unchanged retry is not useful |
| 3 | `RECEIPT_UNSUPPORTED` | Record type or schema version unsupported |
| 4 | `RECEIPT_AUTH_FAILURE` | Controlled security/configuration recovery required |
| 5 | `RECEIPT_EXPIRED` | Remote application rejected the expired record |

Receipt codes are proposed MQTT mappings and MUST be reconciled with `DeliveryResultCode` before approval.

### 16.2 Receipt validation

The device MUST:

- validate the receipt topic and authenticated session;
- require receipt `device_id` to equal the provisioned device;
- correlate `record_id` with an existing or recently completed queued record;
- ignore exact duplicate receipts safely;
- reject a receipt that conflicts with immutable local identity;
- map the receipt to a normalized delivery result;
- let `RemoteDeliveryService`, not the MQTT adapter, finalize the record.

A missing receipt after possible backend acceptance is handled by retrying the same immutable record under the shared delivery policy. It MUST NOT generate a new `record_id`.

## 17. Acceptance matrix

| Message | MQTT QoS acknowledgement | Application-level completion |
| --- | --- | --- |
| Telemetry | Transport evidence | Proposed receipt for strict end-to-end acceptance; final decision open |
| Event | Transport evidence | Proposed receipt for high/critical event records |
| Status | Normally sufficient | Optional backend policy |
| Diagnostic | Normally sufficient | Not required by default |
| Command request | Device received message | `CommandResult` communicates processing outcome |
| Command result | Transport evidence | Proposed receipt required for persistent result |
| Configuration request | Device received message | `ConfigurationResult` communicates processing outcome |
| Configuration result | Transport evidence | Proposed receipt required for persistent result |
| Receipt | Device received message | Local correlation/finalization; no receipt-of-receipt |

There MUST NOT be recursive receipt messages. A receipt is never acknowledged by another application receipt.

## 18. Duplicate and ordering rules

### 18.1 Outbound records

- MQTT QoS 1, reconnect, fallback, and lost responses may create duplicates.
- The backend MUST deduplicate by `(device_id, record_id)` across MQTT and HTTP.
- A duplicate with identical immutable content MUST return the same effective acceptance outcome.
- The same identity with changed immutable content MUST be rejected and diagnosed.
- MQTT Packet Identifier is transport-local and MUST NOT replace `record_id`.
- `record_id` provides identity, not guaranteed chronological ordering.

### 18.2 Commands and configuration

- `command_id` and `request_id` provide idempotency within their authenticated originator scope.
- A duplicate request MUST return/reproduce the known result or remain safely in progress; it MUST NOT repeat a completed state mutation.
- Reuse of an identity with different parameters is a conflict and MUST NOT execute.
- Arrival order does not override revision, freshness, authorization, or product-state validation.

### 18.3 Receipts

- duplicate accepted receipts are harmless;
- a late receipt MAY finalize a matching still-known record according to delivery policy;
- a receipt for an unknown/forgotten identity MUST NOT cause unrelated queue mutation;
- contradictory receipt statuses MUST be diagnosed and resolved by a conservative policy.

## 19. Expiry and stale-message rules

- outbound records use `expires_after_ms` and common time-quality semantics;
- retry does not reset record creation time or lifetime;
- command/configuration requests require bounded freshness before state-changing remote control is enabled;
- retained commands/configurations are prohibited;
- an expired request MUST NOT execute and SHOULD emit an expired result;
- status and Last Will consumers MUST treat messages as observations, not timeless current truth;
- backend processing time and queue time MUST NOT be confused with the record's original sampling time.

## 20. Error behavior

| Error | Receiver behavior | Delivery behavior |
| --- | --- | --- |
| Malformed JSON | Reject before semantic processing | Permanent for unchanged payload |
| Oversized payload | Reject before unbounded allocation | Permanent or configuration fault |
| Unsupported schema | Reject explicitly | `DELIVERY_UNSUPPORTED` or unsupported result |
| Missing/invalid required field | Reject | Permanent for unchanged payload |
| Topic/payload identity mismatch | Reject and security-log | Authorization/contract failure |
| Duplicate identical record | Return prior effective acceptance | Treat as accepted/already accepted |
| Duplicate identity, changed content | Reject and diagnose | Permanent contract violation |
| Temporary backend failure | Do not claim acceptance | Retryable with same identity |
| Unauthorized command/configuration | Do not execute | Rejected result when safe |
| Expired command/configuration | Do not execute | Expired result |

The payload parser MUST validate lengths and structure before copying or allocating variable-size data.

## 21. Payload size and resource limits

Every variable-size object and array MUST have a documented bound. At minimum, the final implementation must freeze:

- maximum MQTT payload bytes;
- maximum event `context` bytes and fields;
- maximum diagnostic context count;
- maximum command/configuration parameter bytes;
- maximum configuration change count;
- maximum field-error count;
- maximum string lengths;
- maximum JSON nesting depth;
- maximum backend deduplication retention.

Until measurements are available, a proposed conservative encoded payload budget is 4096 bytes per MQTT PUBLISH. This is not a capability claim for the EC200U-CN firmware or broker. The final limit MUST fit STM32 buffers, serialization strategy, modem AT/MQTT limits, TLS/MQTT overhead, and backend policy.

Oversized logical records MUST NOT be silently truncated. Fragmentation is not defined for MQTT MVP messages; use a redesigned bounded schema or explicitly versioned fragmentation contract if later required.

## 22. Security requirements

- broker authentication and ACL checks occur before application processing;
- topic identity, authenticated identity, and payload identity MUST agree;
- inbound command/configuration authorization is command/key specific;
- replay protection MUST persist for operations whose duplicate execution could cause unsafe or inconsistent state;
- payloads MUST NOT carry passwords, private keys, tokens, or unrestricted credential material;
- errors MUST avoid exposing secrets or detailed authorization internals;
- diagnostic and free-form fields MUST be bounded and allowlisted;
- MQTT receive MUST NOT directly write arbitrary repository fields or hardware registers.

The full identity and security policy is defined in [../03_security_and_identity.md](../03_security_and_identity.md).

## 23. Change ownership

| Change | Primary document to update | MQTT catalog impact |
| --- | --- | --- |
| Add battery-energy semantic field | Common data contract first | Add JSON mapping to applicable payload; topic normally unchanged |
| Change field unit/type/meaning | Common data contract and versioning | Update mapping and compatibility tests |
| Add a record type | Common data contract | Assign approved topic/message mapping and acceptance rule |
| Rename a topic | Topic namespace | Update every affected catalog entry and migration tests |
| Change QoS/retained | Topic namespace and this catalog | Reassess duplicate, stale, and completion behavior |
| Add a command | Command/security catalog | Define parameters, authorization, idempotency, result, and tests |
| Change receipt requirement | Delivery policy and this catalog | Update finalization and retry behavior |
| Change JSON/CBOR encoding | Versioning and this catalog | Define compatibility boundary and test vectors |

## 24. Verification requirements

`mqtt_test_vectors.md` MUST include at least:

1. valid payload for every enabled message;
2. all required-field omissions;
3. minimum/maximum integer and decimal-string boundaries;
4. malformed and non-canonical `uint64` strings;
5. available zero versus unavailable measurement;
6. reserved and unknown enum/flag behavior;
7. topic/payload `device_id` mismatch;
8. wrong `record_type` on each topic;
9. duplicate identical and duplicate-conflicting records;
10. retry with unchanged `record_id` across MQTT reconnect and HTTP fallback;
11. QoS acknowledgement without application receipt;
12. accepted, retryable, rejected, unsupported, auth-failure, and expired receipts;
13. duplicate/expired/conflicting command and configuration requests;
14. command accepted-intermediate followed by final result;
15. bounded parsing at payload, string, array, and nesting limits;
16. Last Will versus normal device status;
17. sanitized error and diagnostic output.

## 25. Validation gates and open decisions

The following remain **Proposed**:

- JSON as the MVP encoding;
- decimal-string representation for every 64-bit integer;
- final enum numeric assignments not already frozen in the common contract;
- maximum encoded payload and individual collection/string limits;
- exact event and diagnostic catalogs;
- exact command types and parameter schemas;
- exact configuration-key/value representation and allowlist;
- expiry policy when device wall-clock quality is insufficient;
- receipt requirement per outbound record class;
- receipt status codes and backend deduplication retention;
- status/Last Will payload and retained behavior;
- whether diagnostic QoS 0 is ever enabled;
- remote error-code catalogs and safe-detail rules.

This document SHOULD move from `Proposed` to `Approved` only after the common schema, backend validator, firmware serializer/parser, authorization policy, and deterministic test vectors agree.

## 26. References

- [README.md](README.md)
- [mqtt_connection_and_session.md](mqtt_connection_and_session.md)
- [mqtt_topic_namespace.md](mqtt_topic_namespace.md)
- [mqtt_test_vectors.md](mqtt_test_vectors.md)
- [../01_common_data_contract.md](../01_common_data_contract.md)
- [../02_protocol_versioning.md](../02_protocol_versioning.md)
- [../03_security_and_identity.md](../03_security_and_identity.md)
- [../04_error_retry_and_timeout_policy.md](../04_error_retry_and_timeout_policy.md)
- [../05_remote_delivery_policy.md](../05_remote_delivery_policy.md)
