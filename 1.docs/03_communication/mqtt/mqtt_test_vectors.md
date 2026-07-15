# MQTT Test Vectors

| Metadata | Value |
| --- | --- |
| Document ID | COMM-MQTT-TEST-001 |
| Status | Proposed |
| Baseline | Deterministic MQTT contract tests for connection, topics, payloads, delivery, and recovery |
| Applies to | STM32 firmware, Linux simulation, EC200U integration, MQTT broker, backend validators, and integration test automation |

## 1. Purpose

This document defines deterministic positive and negative test vectors for the MQTT communication contract. The same vectors SHOULD be executable against firmware encoders/decoders, the Linux simulator, broker ACLs, backend ingestion, and end-to-end integration tests.

The vectors verify the contracts in:

- [mqtt_connection_and_session.md](mqtt_connection_and_session.md);
- [mqtt_topic_namespace.md](mqtt_topic_namespace.md);
- [mqtt_message_catalog.md](mqtt_message_catalog.md);
- [../01_common_data_contract.md](../01_common_data_contract.md);
- [../05_remote_delivery_policy.md](../05_remote_delivery_policy.md).

## 2. Scope

Coverage includes:

- topic construction and authorization;
- JSON syntax and common-envelope validation;
- 64-bit integer representation;
- measurement availability and quality;
- every MQTT message class;
- duplicate, replay, expiry, and identity mismatch;
- MQTT QoS versus application acceptance;
- connect, subscribe, publish, disconnect, and modem recovery;
- MQTT-to-HTTP fallback without changing record identity;
- payload and resource boundaries;
- log sanitization.

This document does not replace unit tests for measurement algorithms, broker deployment tests, cellular certification, or the final event/command/configuration catalogs.

## 3. Normative language and result states

The terms **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** express requirement strength.

Expected test outcomes use these terms:

| Outcome | Meaning |
| --- | --- |
| `ACCEPT` | Input is valid for the tested contract layer |
| `REJECT_PERMANENT` | Unchanged input must not be retried |
| `REJECT_SECURITY` | Authentication, authorization, identity, or replay rule failed |
| `RETRY_SAME_RECORD` | Transient/ambiguous failure; preserve immutable record and `record_id` |
| `IGNORE_DUPLICATE` | Duplicate is valid but must not repeat a side effect |
| `WAIT_APPLICATION_RECEIPT` | MQTT transport succeeded but application acceptance is still pending |
| `DELIVERED` | Required acceptance evidence has been obtained |
| `CONFIG_BLOCKED` | Static or provisioned configuration prevents safe operation |
| `SECURITY_BLOCKED` | Security remediation is required before retry |

## 4. Fixed test context

Unless a vector overrides a value, use:

| Parameter | Value |
| --- | --- |
| Environment | `prod` |
| MQTT contract major | `1` |
| Device ID | `WM-000001` |
| Other device ID | `WM-000002` |
| MQTT Client ID | `swfpm-WM-000001` |
| MQTT protocol | MQTT 3.1.1 |
| TLS | Enabled with valid server validation and valid device credential |
| Clean Session | `1` |
| Keep Alive | 60 seconds |
| Default QoS | 1 |
| In-flight publish limit | 1 |
| Proposed maximum payload | 4096 encoded bytes |
| Device wall-clock quality | `TIME_SYNCHRONIZED` (`2`) |
| Broker/backend state | Available and authorized |

Canonical topic prefix:

```text
swfpm/prod/v1/devices/WM-000001
```

## 5. Test harness requirements

The test harness MUST be able to:

1. inject connection, broker, subscription, publish, timeout, and asynchronous modem results;
2. control monotonic and wall-clock time independently;
3. inspect MQTT state, active connection generation, queue state, attempt state, and normalized result;
4. publish and subscribe using device and backend credentials;
5. simulate reconnect, modem reset, delayed URCs, duplicate delivery, and lost application receipt;
6. compare payloads byte-for-byte where canonical retransmission is required and semantically where JSON member order is irrelevant;
7. verify that secrets are absent from logs;
8. reset persistent command/request deduplication state between isolated cases.

Each automated result SHOULD record the document version, firmware build, modem firmware, broker version/configuration, and backend validator version.

## 6. Canonical valid telemetry fixture

Identifier: `FIX-TELEMETRY-001`

Topic:

```text
swfpm/prod/v1/devices/WM-000001/up/telemetry
```

QoS: `1`  
Retained: `false`

Payload:

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

Expected:

- encoder output satisfies the proposed JSON mapping;
- backend returns `ACCEPT`;
- topic, authenticated identity, and payload identity agree;
- MQTT QoS acknowledgement yields `WAIT_APPLICATION_RECEIPT` when telemetry receipts are enabled, otherwise the configured acceptance policy applies;
- retry uses `record_id = "4294967297"` and the same immutable payload.

## 7. Topic namespace vectors

| ID | Actor/action | Topic/filter | Expected |
| --- | --- | --- | --- |
| `TOP-001` | Device publishes telemetry | `swfpm/prod/v1/devices/WM-000001/up/telemetry` | `ACCEPT` |
| `TOP-002` | Device publishes event | `swfpm/prod/v1/devices/WM-000001/up/events` | `ACCEPT` |
| `TOP-003` | Device subscribes command | `swfpm/prod/v1/devices/WM-000001/down/commands` | `ACCEPT` when commands enabled |
| `TOP-004` | Device publishes to other device | `swfpm/prod/v1/devices/WM-000002/up/telemetry` | `REJECT_SECURITY` by broker ACL |
| `TOP-005` | Device subscribes other device | `swfpm/prod/v1/devices/WM-000002/down/commands` | `REJECT_SECURITY` |
| `TOP-006` | Device publishes to downlink | `swfpm/prod/v1/devices/WM-000001/down/commands` | `REJECT_SECURITY` |
| `TOP-007` | Device subscribes uplink | `swfpm/prod/v1/devices/WM-000001/up/#` | `REJECT_SECURITY` |
| `TOP-008` | Device uses wildcard in publish | `swfpm/prod/v1/devices/+/up/telemetry` | Reject locally and/or by broker |
| `TOP-009` | Device uses wrong environment | `swfpm/dev/v1/devices/WM-000001/up/telemetry` | `REJECT_SECURITY` |
| `TOP-010` | Device uses unsupported major | `swfpm/prod/v2/devices/WM-000001/up/telemetry` | `CONFIG_BLOCKED` locally or contract rejection remotely |
| `TOP-011` | Missing direction segment | `swfpm/prod/v1/devices/WM-000001/telemetry` | `REJECT_PERMANENT` |
| `TOP-012` | Wrong direction for telemetry | `swfpm/prod/v1/devices/WM-000001/down/telemetry` | `REJECT_PERMANENT` |
| `TOP-013` | Leading slash | `/swfpm/prod/v1/devices/WM-000001/up/telemetry` | `REJECT_PERMANENT` |
| `TOP-014` | Trailing slash | `swfpm/prod/v1/devices/WM-000001/up/telemetry/` | `REJECT_PERMANENT` |
| `TOP-015` | Empty device ID | `swfpm/prod/v1/devices//up/telemetry` | `REJECT_PERMANENT` |
| `TOP-016` | Unknown message class | `swfpm/prod/v1/devices/WM-000001/up/battery` | `REJECT_PERMANENT` |
| `TOP-017` | Backend telemetry filter | `swfpm/prod/v1/devices/+/up/telemetry` | Receives valid production telemetry only |
| `TOP-018` | Normal device subscribes broad filter | `swfpm/prod/v1/#` | `REJECT_SECURITY` |

`TOP-016` verifies that adding a battery field does not automatically create a new MQTT topic.

## 8. JSON and common-envelope vectors

All mutations in this section start from `FIX-TELEMETRY-001`.

| ID | Mutation | Expected |
| --- | --- | --- |
| `JSON-001` | No mutation | `ACCEPT` |
| `JSON-002` | Remove `schema_version` | `REJECT_PERMANENT` |
| `JSON-003` | Remove `record_id` | `REJECT_PERMANENT` |
| `JSON-004` | Set `record_id` to `"0"` | `REJECT_PERMANENT` |
| `JSON-005` | Encode `record_id` as JSON number `4294967297` | Reject non-canonical 64-bit representation |
| `JSON-006` | Set `record_id` to `"04294967297"` | Reject leading zero |
| `JSON-007` | Set `record_id` to `"+4294967297"` | Reject leading plus |
| `JSON-008` | Set `record_id` to `" 4294967297"` | Reject whitespace |
| `JSON-009` | Set `record_id` to `"18446744073709551616"` | Reject `uint64` overflow |
| `JSON-010` | Set `flow_rate.value` to JSON number `125000` | Reject non-canonical `int64` representation |
| `JSON-011` | Set `flow_rate.value` to `"-9223372036854775808"` | `ACCEPT` at representation layer; domain range may reject |
| `JSON-012` | Set `flow_rate.value` to `"9223372036854775808"` | Reject `int64` overflow |
| `JSON-013` | Add duplicate JSON member `record_id` | Reject before semantic processing |
| `JSON-014` | Add trailing comma | Reject malformed JSON |
| `JSON-015` | Insert JSON comment | Reject malformed JSON |
| `JSON-016` | Set `pressure.value` to `NaN` | Reject malformed/non-finite JSON number |
| `JSON-017` | Set required `payload` to `null` | `REJECT_PERMANENT` |
| `JSON-018` | Set `record_type` to `2` on telemetry topic with telemetry payload | `REJECT_PERMANENT` |
| `JSON-019` | Set `device_id` to `WM-000002` while topic remains device 1 | `REJECT_SECURITY` |
| `JSON-020` | Add unknown optional member under compatible schema | Ignore/preserve according to versioning policy; do not reinterpret |
| `JSON-021` | Set unsupported `schema_version` | Explicit unsupported result; no unsafe partial processing |
| `JSON-022` | Set reserved flag bit not allowed by schema | Reject or preserve/report according to versioning policy; never relabel |

## 9. Measurement-quality vectors

### 9.1 Available zero

Mutation to telemetry pressure:

```json
{
  "value": 0,
  "quality_flags": 1,
  "age_ms": 0,
  "sampled_at_unix_ms": "1784102400000"
}
```

Expected `MEAS-001`: `ACCEPT`; zero is a real pressure value and MUST NOT mean unavailable.

### 9.2 Unavailable measurement

```json
{
  "quality_flags": 2,
  "age_ms": 4294967295
}
```

Expected `MEAS-002`: `ACCEPT`; numeric `value` is omitted and `QUALITY_UNAVAILABLE` is set.

### 9.3 Contradictory quality

```json
{
  "value": 315000,
  "quality_flags": 3,
  "age_ms": 250
}
```

Expected `MEAS-003`: reject because `QUALITY_VALID` and `QUALITY_UNAVAILABLE` are both set.

### 9.4 Null unavailable value

```json
{
  "value": null,
  "quality_flags": 2,
  "age_ms": 4294967295
}
```

Expected `MEAS-004`: reject non-canonical use of `null`; omit `value` instead.

## 10. Message-class vectors

| ID | Message | Key assertion | Expected |
| --- | --- | --- | --- |
| `MSG-001` | Telemetry | Valid fixture on telemetry topic | `ACCEPT` |
| `MSG-002` | Event | `record_type=2`, bounded context, event topic | `ACCEPT` |
| `MSG-003` | Event | `occurrence_count=0` | Reject; minimum is 1 |
| `MSG-004` | Status | Normal enveloped `DeviceStatusRecord` | `ACCEPT` |
| `MSG-005` | Status | Last Will minimal presence object | Accept only as Last Will/presence schema, not common record |
| `MSG-006` | Diagnostic | `context_u32` contains 4 values | `ACCEPT` |
| `MSG-007` | Diagnostic | `context_u32` contains 5 values | Reject bound violation |
| `MSG-008` | Diagnostic | Contains raw AT transcript with credential | Reject/security-log without exposing secret |
| `MSG-009` | Command request | Authorized, valid, fresh, unique | Accept for dispatch and publish correlated result |
| `MSG-010` | Command result | Same `command_id`, new outbound `record_id` | `ACCEPT` |
| `MSG-011` | Configuration request | Valid `base_revision` and allowlisted key | Validate/apply according to `apply_mode` |
| `MSG-012` | Configuration request | Unknown configuration key | Reject; do not persist |
| `MSG-013` | Configuration result | Does not echo protected values | `ACCEPT` |
| `MSG-014` | Application receipt | Matches queued record and accepted status | `DELIVERED` through `RemoteDeliveryService` |
| `MSG-015` | Application receipt | Receipt-of-receipt requested | Reject; recursive receipts are prohibited |

Final event enum codes, command types, configuration keys, and Last Will schema remain proposed; vectors using them become normative only when their owning catalogs are approved.

## 11. Command and configuration replay vectors

Canonical command request:

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

| ID | Condition | Expected |
| --- | --- | --- |
| `CMD-001` | First authorized delivery before expiry | Execute at most once; publish result |
| `CMD-002` | Exact duplicate while first execution in progress | `IGNORE_DUPLICATE`; report accepted/in-progress without second mutation |
| `CMD-003` | Exact duplicate after final success | Do not execute again; reproduce/return known final outcome |
| `CMD-004` | Same `command_id`, changed parameters | `REJECT_SECURITY` or conflict; no execution |
| `CMD-005` | Command received after `expires_at_unix_ms` | Do not execute; publish expired result |
| `CMD-006` | Unsupported `command_type` | Do not execute; publish unsupported result |
| `CMD-007` | Valid syntax but principal not authorized for type | Do not execute; publish bounded rejected result when safe |
| `CMD-008` | Device cannot establish trustworthy freshness | Reject/defer according to approved safe policy; never silently bypass expiry |
| `CMD-009` | Retained command delivered after subscribe | Reject retained command; no execution |
| `CMD-010` | Reboot after state mutation but before result publish | Persistent dedupe prevents repeat; publish/recover final result |

Equivalent cases MUST be implemented for configuration `request_id`, including base-revision conflict, unknown key, cross-field failure, validate-only mode, safe deferred apply, and duplicate after reboot.

## 12. Application receipt vectors

Canonical accepted receipt:

```json
{
  "schema_version": 1,
  "device_id": "WM-000001",
  "record_id": "4294967297",
  "receipt_status": 0,
  "received_at_unix_ms": "1784102401000",
  "remote_receipt_id": "rcpt-01J2ABCDEF",
  "error_code": 0,
  "retry_after_ms": 0
}
```

| ID | Mutation/state | Expected |
| --- | --- | --- |
| `RCT-001` | Matching queued record, accepted status | Map to `DELIVERY_ACCEPTED`; delivery service finalizes record |
| `RCT-002` | Exact duplicate accepted receipt | `IGNORE_DUPLICATE`; record remains delivered |
| `RCT-003` | Receipt device ID differs from provisioned device | `REJECT_SECURITY` |
| `RCT-004` | Unknown `record_id` | No unrelated queue mutation; bounded diagnostic |
| `RCT-005` | Retryable receipt with `retry_after_ms=60000` | Preserve same record; schedule no earlier than policy permits |
| `RCT-006` | Permanent rejected receipt | Map to terminal failure for unchanged payload |
| `RCT-007` | Unsupported schema/record receipt | Map to `DELIVERY_UNSUPPORTED` |
| `RCT-008` | Auth-failure receipt | Enter controlled security/config recovery; no fast retry loop |
| `RCT-009` | Accepted receipt lost; publish retried | Backend deduplicates same `(device_id, record_id)` and returns same effective acceptance |
| `RCT-010` | Contradictory accepted then rejected receipt | Diagnose; do not resurrect/delete unrelated data; conservative policy applies |

## 13. Connection and session vectors

| ID | Preconditions/action | Expected state/result |
| --- | --- | --- |
| `CONN-001` | Valid config, network ready, TLS and broker accept, no downlink enabled | `IDLE → WAIT_NETWORK → OPENING → CONNECTING → READY` |
| `CONN-002` | Commands enabled and SUBACK succeeds | Enter `READY` only after command subscription acknowledgement |
| `CONN-003` | Mandatory subscription rejected | Do not enter `READY`; bounded error/backoff |
| `CONN-004` | Broker host missing | `CONFIG_BLOCKED`; no retry loop |
| `CONN-005` | Certificate validation fails | `SECURITY_BLOCKED` after bounded policy |
| `CONN-006` | Credential rejected | `SECURITY_BLOCKED`; no unlimited reconnect |
| `CONN-007` | Network registration unavailable | `WAIT_NETWORK` then bounded timeout/backoff |
| `CONN-008` | Broker connection times out | Invalidate attempt; enter bounded backoff |
| `CONN-009` | Clean Session reconnect | Restore every mandatory subscription before `READY` |
| `CONN-010` | Packet-data context recreated | Previous MQTT session is invalid; reconnect fully |
| `CONN-011` | Modem reset while `READY` | Invalidate session and enter `RECOVERING` |
| `CONN-012` | Graceful batch completion | Stop new operations, finish bounded in-flight work, disconnect, enter `IDLE` |
| `CONN-013` | Unexpected disconnect | Last Will routed to status service; device enters backoff/recovery |
| `CONN-014` | Duplicate active Client ID | Older session disconnect is diagnosed as identity/session conflict |
| `CONN-015` | Keep Alive failure | Session invalidated; bounded reconnect |
| `CONN-016` | HTTP owns modem operation | MQTT does not issue conflicting operation |

## 14. In-flight and asynchronous result vectors

| ID | Sequence | Expected |
| --- | --- | --- |
| `ASYNC-001` | Publish A starts; publish B requested before A terminal result | B remains queued/rejected busy; only one in flight |
| `ASYNC-002` | Connection generation 10 times out; generation 11 connects; late success for 10 arrives | Ignore stale generation-10 result |
| `ASYNC-003` | Publish operation 20 times out; late ACK for 20 arrives after operation 21 starts | Late result MUST NOT complete operation 21 |
| `ASYNC-004` | Modem reset clears context; old URC reports connected | Ignore old URC; session remains invalid |
| `ASYNC-005` | Publish QoS ACK received for matching active operation | Complete transport layer only; apply message acceptance matrix |
| `ASYNC-006` | Malformed/unexpected URC | Sanitized diagnostic; bounded recovery; no state corruption |

Correlation MUST include connection generation, local operation ID, topic/message class, and record identity where applicable.

## 15. Delivery, retry, duplicate, and fallback vectors

### 15.1 QoS success without application receipt

| Step | Action | Expected |
| ---: | --- | --- |
| 1 | Queue telemetry record `4294967297` | Record identity fixed |
| 2 | Publish with MQTT QoS 1 | One active attempt |
| 3 | Receive MQTT transport acknowledgement | `DELIVERY_TRANSPORT_ACK_ONLY` |
| 4 | Receipt policy requires application acceptance | Record remains pending; `WAIT_APPLICATION_RECEIPT` |
| 5 | Receive accepted application receipt | `DELIVERED` |

Identifier: `DLV-001`.

### 15.2 Lost acknowledgement and retry

| Step | Action | Expected |
| ---: | --- | --- |
| 1 | Publish event record `4294967298` | Backend may accept |
| 2 | Connection drops before device sees sufficient acknowledgement | Outcome ambiguous |
| 3 | Backoff expires | `RETRY_SAME_RECORD` |
| 4 | Republish | Same `device_id`, `record_id`, and immutable semantic payload |
| 5 | Backend detects duplicate | Return same effective acceptance; no duplicate logical event |

Identifier: `DLV-002`.

### 15.3 MQTT-to-HTTP fallback

| Step | Action | Expected |
| ---: | --- | --- |
| 1 | Record `4294967297` is reserved for MQTT | Exactly one active attempt |
| 2 | MQTT reaches policy failure threshold | MQTT attempt ends; record remains queued |
| 3 | `RemoteDeliveryService` selects HTTP | No simultaneous MQTT publish |
| 4 | HTTP serializes the record | Preserve `(WM-000001, 4294967297)` and immutable semantic values |
| 5 | HTTP reports accepted/already accepted | One logical remote record becomes delivered |

Identifier: `DLV-003`.

### 15.4 Additional delivery matrix

| ID | Condition | Expected |
| --- | --- | --- |
| `DLV-004` | MQTT publish permanent payload rejection | Do not retry unchanged record over MQTT or HTTP unless mapping difference is explicitly relevant |
| `DLV-005` | Shared cellular network unavailable | Do not thrash between MQTT and HTTP; common backoff applies |
| `DLV-006` | MQTT security blocked, HTTP independently valid | Fallback only if policy and independent authorization allow |
| `DLV-007` | Shared credential revoked | Both channels blocked; no false fallback recovery |
| `DLV-008` | Record expires during backoff | Mark expired; do not transmit |
| `DLV-009` | MQTT recovers after HTTP fallback | New work may fail back at safe boundary; active HTTP attempt is not interrupted |
| `DLV-010` | Retry code creates new `record_id` | Test fails; identity must remain stable |
| `DLV-011` | Retry resamples telemetry under old `record_id` | Test fails; immutable content violation |
| `DLV-012` | Queue record accepted twice through MQTT and HTTP | Backend converges to one logical record |

## 16. Retained-message and Last Will vectors

| ID | Condition | Expected |
| --- | --- | --- |
| `RET-001` | Telemetry published with retain flag | Reject locally/test failure; telemetry MUST NOT retain |
| `RET-002` | Event published retained | Reject locally/test failure |
| `RET-003` | Command published retained | Device refuses execution; security/contract diagnostic |
| `RET-004` | Configuration published retained | Device refuses apply |
| `RET-005` | Receipt published retained | Device refuses it as non-canonical |
| `RET-006` | Last Will configured on own `up/status` | Broker accepts when ACL permits |
| `RET-007` | Last Will configured on other device status | Broker rejects ACL |
| `RET-008` | Unexpected disconnect fires Will | Status consumer treats as unexpected MQTT offline evidence only |
| `RET-009` | Device reconnects after Will | Normal online/status publication supersedes prior session observation |
| `RET-010` | Retained status enabled without timestamp/stale policy | Configuration/contract test fails |

## 17. Boundary and robustness vectors

| ID | Input | Expected |
| --- | --- | --- |
| `BND-001` | Encoded payload exactly approved maximum | Accept if all semantic fields valid |
| `BND-002` | Encoded payload one byte above maximum | Reject before unbounded allocation |
| `BND-003` | Complete topic exactly approved maximum | Accept |
| `BND-004` | Topic one byte above maximum | Reject construction before modem call |
| `BND-005` | `device_id` length 48, valid characters | Accept if complete topic fits |
| `BND-006` | `device_id` length 49 | Reject |
| `BND-007` | Event context at approved field/byte bound | Accept |
| `BND-008` | Event context exceeds bound | Reject; do not truncate silently |
| `BND-009` | Diagnostic context has 4 `uint32` values | Accept |
| `BND-010` | Diagnostic context has 5 values | Reject |
| `BND-011` | JSON nesting at approved maximum | Accept |
| `BND-012` | JSON nesting above maximum | Reject before stack/resource exhaustion |
| `BND-013` | Maximum `uint64` string `"18446744073709551615"` | Representation accepts; domain may constrain |
| `BND-014` | Minimum `int64` string `"-9223372036854775808"` | Representation accepts; domain may constrain |
| `BND-015` | Oversized logical record needing fragmentation | Reject/design error; MQTT MVP fragmentation is undefined |

The current 4096-byte proposal is a test parameter, not a confirmed modem capability. When the final limit changes, `BND-001` and `BND-002` MUST be generated from the approved value.

## 18. Time and expiry vectors

| ID | Input/state | Expected |
| --- | --- | --- |
| `TIME-001` | `created_at_unix_ms="0"`, `TIME_UNKNOWN` | Accept only where common contract permits |
| `TIME-002` | `created_at_unix_ms="0"`, `TIME_SYNCHRONIZED` | Reject inconsistent time state |
| `TIME-003` | Record still within lifetime | Eligible for delivery |
| `TIME-004` | Record exceeds explicit lifetime | Expire; no publish |
| `TIME-005` | Retry occurs | Original creation timestamp and lifetime retained |
| `TIME-006` | Command received before expiry with trusted time | Eligible after other checks |
| `TIME-007` | Command received after expiry | Do not execute |
| `TIME-008` | Device time untrusted for state-changing command | Apply approved conservative policy; never assume fresh silently |
| `TIME-009` | Wall clock corrected after record creation | Do not rewrite record identity or original sample semantics |

## 19. Security and log-sanitization vectors

| ID | Test | Expected |
| --- | --- | --- |
| `SEC-001` | Broker certificate invalid | Connection rejected; `SECURITY_BLOCKED` after bounded policy |
| `SEC-002` | Device credential invalid | Authentication rejected; secret not logged |
| `SEC-003` | Topic/payload/authenticated identity mismatch | `REJECT_SECURITY` |
| `SEC-004` | Unauthorized command type | No execution; bounded result/diagnostic |
| `SEC-005` | Command contains arbitrary executable text/pass-through | Reject |
| `SEC-006` | Configuration attempts raw credential echo/update outside provisioning | Reject; no value echoed |
| `SEC-007` | Diagnostic contains token-like secret fixture | Reject or redact according to safe policy; fixture MUST NOT reach production logs |
| `SEC-008` | Modem error includes credential-bearing command text | Log normalized code only; no full transcript |
| `SEC-009` | Cross-environment credential use | Broker ACL rejects |
| `SEC-010` | Replayed state-changing command after reboot | Persistent dedupe prevents repeated mutation |

Test secrets MUST be synthetic and clearly non-production.

## 20. Queue and power-policy vectors

| ID | Condition | Expected |
| --- | --- | --- |
| `QUEUE-001` | No eligible records and no downlink requirement | MQTT remains `IDLE`/disconnects according to power policy |
| `QUEUE-002` | Batch of queued telemetry | Connect once, publish serially, finalize per acceptance rule, disconnect |
| `QUEUE-003` | High-priority event plus normal telemetry | Scheduler follows priority/fairness policy without changing payload identity |
| `QUEUE-004` | Queue record reserved by MQTT | HTTP cannot simultaneously reserve the same record |
| `QUEUE-005` | Publish attempt cancelled before acceptance | Record returns to eligible retry state |
| `QUEUE-006` | Modem recovery required | Queue remains intact across modem reset |
| `QUEUE-007` | Application queue full | Apply documented queue/loss policy; broker session queue is not used as replacement |
| `QUEUE-008` | Delivery batch complete | Graceful disconnect; modem ownership released |

## 21. Required assertions per implementation layer

| Layer | Minimum assertions |
| --- | --- |
| Common record builder | Stable identity, units, quality, timestamps, immutable record content |
| MQTT encoder | Correct topic selection, JSON types, 64-bit decimal strings, bounded size |
| MQTT decoder | Syntax, bounds, schema, identity, replay, expiry, command/config authorization |
| MQTT state machine | State transitions, generation correlation, one in-flight operation, clean-session subscriptions |
| EC200U integration | Serialized operations, timeout, stale URC rejection, normalized results, recovery |
| Broker | TLS/authentication, per-device ACL, wildcard routing, retained restrictions where enforceable |
| Backend ingestion | Schema validation, cross-channel dedupe, immutable-content conflict, receipt semantics |
| `RemoteDeliveryService` | Queue ownership, acceptance interpretation, retry, expiry, fallback/failback |

## 22. Suggested machine-readable vector format

Automated suites MAY mirror each vector in a separate machine-readable fixture. Proposed shape:

```json
{
  "test_id": "JSON-006",
  "contract_version": "COMM-MQTT-TEST-001/proposed-1",
  "layer": "backend_decoder",
  "topic": "swfpm/prod/v1/devices/WM-000001/up/telemetry",
  "qos": 1,
  "retain": false,
  "fixture": "FIX-TELEMETRY-001",
  "mutation": {
    "json_pointer": "/record_id",
    "operation": "replace",
    "value": "04294967297"
  },
  "expected": {
    "outcome": "REJECT_PERMANENT",
    "error_class": "INVALID_INTEGER_ENCODING"
  }
}
```

This Markdown document remains the normative human-readable index until a fixture directory and schema are formally added to the repository. Machine-readable files MUST carry stable test IDs that map back to this document.

## 23. Exit criteria

The MQTT implementation is ready for integration review when:

- all enabled-message positive vectors pass on firmware/simulator and backend;
- all negative parser, identity, ACL, replay, retained, and size vectors reject safely;
- connection/session vectors pass with the selected broker and EC200U-CN firmware;
- stale asynchronous modem results cannot corrupt a newer operation;
- MQTT QoS and application acceptance are never conflated;
- retries and MQTT/HTTP fallback preserve `(device_id, record_id)` and immutable semantics;
- command/configuration duplicates cannot repeat state mutation;
- payload/topic limits are measured and frozen;
- logs pass secret-sanitization checks;
- vector results are recorded for the released firmware/backend combination.

## 24. Validation gates and open work

Before this document can become `Approved`, the project must freeze:

- exact MQTT payload-byte and topic-byte limits;
- exact enum numeric catalogs used by events, diagnostics, commands, and configuration;
- command and configuration allowlists;
- application-receipt requirement per record class;
- Last Will/status schema and retained policy;
- expiry behavior with insufficient wall-clock quality;
- broker ACL implementation and environment separation;
- EC200U-CN firmware behavior and timing limits;
- machine-readable fixture location, schema, and CI runner;
- expected normalized error codes for every negative vector.

## 25. References

- [README.md](README.md)
- [mqtt_connection_and_session.md](mqtt_connection_and_session.md)
- [mqtt_topic_namespace.md](mqtt_topic_namespace.md)
- [mqtt_message_catalog.md](mqtt_message_catalog.md)
- [../01_common_data_contract.md](../01_common_data_contract.md)
- [../02_protocol_versioning.md](../02_protocol_versioning.md)
- [../03_security_and_identity.md](../03_security_and_identity.md)
- [../04_error_retry_and_timeout_policy.md](../04_error_retry_and_timeout_policy.md)
- [../05_remote_delivery_policy.md](../05_remote_delivery_policy.md)
