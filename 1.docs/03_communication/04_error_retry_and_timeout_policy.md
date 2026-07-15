# Error, Retry, and Timeout Policy

| Metadata | Value |
|---|---|
| Document ID | `COMM-RELIABILITY-004` |
| Status | `Proposed` |
| Baseline | Bounded retries, monotonic deadlines, transport-neutral record lifecycle, independent channel health |
| Applies to | RemoteDeliveryService, MQTT, HTTP, BLE, STM32–nRF52810 link, STM32–EC200U-CN integration, Linux simulation, and STM32 platform implementation |

## 1. Purpose

This document defines error classification, timeout, retry, backoff, queue-overflow, expiration, fallback, and recovery policy for the communication subsystem.

It ensures that communication failures:

- remain bounded and diagnosable;
- do not block the cooperative event loop;
- do not stop measurement, volume accumulation, leak evaluation, or local display;
- do not cause unbounded queue or retry growth;
- preserve stable record and command identity;
- do not create uncontrolled duplicate delivery across MQTT and HTTP;
- and can be reproduced deterministically in Linux simulation.

This document defines shared reliability semantics. Channel-selection order and primary/fallback rules are finalized in `05_remote_delivery_policy.md`. Exact MQTT packets, HTTP responses, BLE messages, and AT-command sequences remain in their protocol-specific documents.

## 2. Reliability objectives

The communication system shall:

1. use monotonic deadlines for all local timeout decisions;
2. avoid unbounded blocking and busy waiting;
3. classify failures before selecting retry behavior;
4. bound retries by attempt count, elapsed time, and record lifetime;
5. separate record retry state from protocol transaction state;
6. distinguish channel failure from record rejection;
7. preserve `record_id` across retry and MQTT/HTTP fallback;
8. make retry safe through idempotency and deduplication;
9. protect low-power residency during extended outages;
10. prioritize important records without starving ordinary records indefinitely;
11. recover adapters and modem ownership after timeout/reset;
12. expose normalized diagnostics without leaking secrets.

## 3. Normative terminology

| Term | Meaning |
|---|---|
| Attempt | One bounded adapter operation for one record or HTTP batch |
| Transaction | Protocol-specific operation from start until completion/cancel/timeout |
| Deadline | Absolute monotonic time after which an operation is late |
| Timeout | Result produced when a deadline is reached before valid completion |
| Retry | New attempt of the same immutable application record |
| Immediate retry | Retry without normal backoff, allowed only for narrowly defined transient cases |
| Backoff | Delay before another eligible attempt |
| Retry budget | Maximum permitted attempts and/or elapsed retry time |
| Fallback | Attempting the same record through another eligible channel |
| Permanent failure | Failure for which retrying the unchanged operation is not useful or allowed |
| Channel health | Normalized assessment of a channel's ability to accept new attempts |
| Circuit open | Channel temporarily ineligible after repeated failures |
| Expiration | Record no longer eligible for delivery because its lifetime ended |
| Abandon | Terminal local removal according to documented loss/audit policy |

## 4. Architectural boundaries

Responsibilities are separated as follows:

| Component | Owns | Does not own |
|---|---|---|
| Scheduler | Retry/maintenance deadline events | Network transaction execution |
| Delivery queue/repository | Record storage, reservation, expiration metadata | MQTT/HTTP protocol state |
| `RemoteDeliveryService` | Attempt selection, retry budget, fallback decision request, final delivery state | AT parsing, MQTT packet flow, HTTP body parsing |
| MQTT adapter | MQTT session and publish/subscribe transaction timeout | Record deletion and fallback policy |
| HTTP adapter | HTTP request/response and per-record batch result | Queue overflow and final record lifecycle |
| Modem service | EC200U-CN power/network/AT ownership and recovery | Product record priority |
| BLE service | BLE transaction/request timeout | Direct product-state mutation |
| Internal-link protocol | Frame/ACK/reassembly timeout | Application command authorization |
| Power manager | Sleep eligibility and power lease | Reclassifying errors as success |

## 5. General invariants

1. Every asynchronous operation has a start condition, one owner, one deadline, and one terminal result.
2. A completion received after timeout is stale unless explicitly matched to a still-valid attempt.
3. Timeout cleanup releases owned buffers, modem/link reservations, and power leases.
4. Retry creates a new attempt but does not create a new application record.
5. Protocol identifiers may change between attempts; `record_id` does not.
6. A retry shall not modify original sample values, timestamps, or quality flags.
7. Error recovery shall not recurse or loop within one event-loop turn.
8. Retry counters saturate and shall not wrap.
9. Unknown errors default to non-immediate retry with diagnostics; they shall not be treated as success.
10. A permanent record rejection shall not mark the entire channel unhealthy unless evidence indicates channel-wide failure.

## 6. Error taxonomy

### 6.1 Error scope

| Scope | Meaning | Example |
|---|---|---|
| Record | Specific record/payload cannot be accepted | Unsupported schema, invalid field |
| Transaction | One attempt failed | Response timeout, publish interrupted |
| Session | MQTT/HTTP/TLS session unusable | Broker disconnect, TLS alert |
| Channel | MQTT or HTTP temporarily unavailable | Repeated server failure |
| Network | Cellular registration/PDP unavailable | Network lost |
| Component | nRF52810/modem/driver requires reset or service | Parser desynchronization |
| Security | Authentication, authorization, trust, replay issue | Invalid certificate, revoked credential |
| Configuration | Required profile/endpoint/credential missing or invalid | No broker/server profile |
| Resource | Queue, memory, buffer, or storage limit reached | Queue full |

### 6.2 Error persistence

| Class | Meaning |
|---|---|
| Transient | Expected to clear without configuration or firmware change |
| Persistent | Remains until reset, reprovisioning, configuration, or repair |
| Permanent for record | Unchanged record will not become acceptable |
| Unknown | Insufficient evidence; handled conservatively and diagnosed |

### 6.3 Retry classification

| Classification | Default action |
|---|---|
| `SUCCESS` | Complete record according to delivery contract |
| `ACK_ONLY` | Wait for required application receipt or apply documented success rule |
| `RETRYABLE_SAME_SESSION` | Retry only within immediate budget if safe |
| `RETRYABLE_NEW_SESSION` | Close/recreate protocol session, then backoff/retry |
| `RETRYABLE_NEW_NETWORK` | Recover modem/network before new attempt |
| `FALLBACK_ELIGIBLE` | Ask delivery policy for another channel after normal cleanup |
| `PERMANENT_RECORD_FAILURE` | Terminal record failure; do not retry unchanged payload |
| `SECURITY_RECOVERY_REQUIRED` | Stop normal retries; enter bounded credential/trust recovery |
| `CONFIGURATION_REQUIRED` | Stop channel attempts until relevant config revision changes |
| `COMPONENT_RECOVERY_REQUIRED` | Reset/reinitialize bounded component state |

## 7. Timeout model

### 7.1 Absolute monotonic deadlines

All local timeouts shall use an absolute monotonic deadline:

```text
deadline_ms = start_monotonic_ms + timeout_ms
expired = now_monotonic_ms >= deadline_ms
```

Implementations shall handle counter wrap using the repository's approved monotonic-time comparison method. Wall-clock RTC time shall not determine local transaction timeout.

### 7.2 Timeout hierarchy

| Timeout | Purpose |
|---|---|
| Byte/inter-byte | Detect incomplete frame or stream gap where framing requires it |
| Driver operation | Bound UART/DMA/GPIO/platform completion |
| Command response | Bound one AT/internal-link/BLE command response |
| Protocol transaction | Bound MQTT publish or HTTP request/response |
| Session establishment | Bound TLS/MQTT/HTTP session setup |
| Network establishment | Bound modem start, registration, and data context |
| Overall delivery attempt | Bound the complete adapter attempt including prerequisites |
| Record lifetime | Bound how long a record remains deliverable |
| Commissioning/session inactivity | Bound local authorization/session validity |

An inner timeout shall not extend an already-established outer deadline.

### 7.3 Deadline propagation

A caller provides an overall deadline or budget to lower layers. A lower layer uses:

```text
effective_deadline = min(caller_deadline, local_operation_deadline)
```

Lower layers shall not silently restart the caller's full timeout after partial progress.

## 8. Proposed timeout baseline

Values below are initial engineering baselines, not hardware-qualified guarantees.

| Symbol | Proposed value | Applies to | Validation requirement |
|---|---:|---|---|
| `NRF_FRAME_INTERBYTE_MS` | 100 ms | Incomplete STM32–nRF frame | Validate at selected baud rate and maximum frame |
| `NRF_COMMAND_RESPONSE_MS` | 1,000 ms | Ordinary internal command response | Measure nRF processing and BLE-stack latency |
| `BLE_APP_COMMAND_MS` | 10,000 ms | End-to-end bounded BLE application command | Separate long persistent operations if required |
| `MODEM_AT_RESPONSE_MS` | 5,000 ms | Ordinary EC200U AT response | Override only per documented long-running command |
| `MODEM_BOOT_READY_MS` | 30,000 ms | Modem boot/readiness | Hardware measurement required |
| `NETWORK_REGISTRATION_MS` | 180,000 ms | Cellular registration attempt | Test weak/no-network cases |
| `DATA_CONTEXT_MS` | 60,000 ms | PDP/data context activation | Test carrier/APN profiles |
| `MQTT_CONNECT_MS` | 60,000 ms | TLS plus MQTT connection | Measure broker/modem behavior |
| `MQTT_PUBLISH_RESULT_MS` | 30,000 ms | Required publish/result evidence | Depends on QoS/application receipt |
| `HTTP_TRANSACTION_MS` | 60,000 ms | One HTTP request and response | Confirm body size, TLS reuse, network quality |
| `MODEM_RECOVERY_MS` | 60,000 ms | Controlled modem reset/reinitialization | Hardware validation required |

Rules:

- A protocol document may define a smaller value.
- A larger value requires rationale and an outer bounded deadline.
- Timeout constants are configuration/build policy, not remote-controlled arbitrary values.
- Hardware qualification shall record observed distribution and margin.

## 9. Timeout completion race

Completion and timeout may become eligible during the same scheduler turn.

Required rule:

1. Each attempt has a unique local `attempt_id` and generation/session identifier.
2. Exactly one terminal transition may win.
3. Once timeout cleanup commits, later matching completion is stale and ignored/diagnosed.
4. A stale completion shall not release resources owned by a newer attempt.
5. Linux tests shall exercise completion exactly before, at, and after deadline.

## 10. Retry budget

Retry eligibility is constrained by all of:

- error classification;
- attempt count;
- immediate-retry count;
- elapsed time since first attempt;
- record expiration;
- channel circuit state;
- modem/component recovery budget;
- power policy;
- security/configuration state;
- fallback policy.

A retry is allowed only if every applicable constraint allows it.

### 10.1 Proposed default budgets

| Budget | Proposed baseline |
|---|---:|
| Immediate retries per transaction class | Maximum 1 |
| Attempts before channel-health penalty | 3 consecutive retryable failures |
| Component resets per delivery cycle | Maximum 1 |
| Modem full resets within 30 minutes | Maximum 2 |
| Authentication retries before security backoff | Maximum 2 normal attempts |
| Record attempts | Controlled by record lifetime and delivery policy; shall have explicit maximum |

The exact maximum record attempts remains open because telemetry lifetime, reporting period, queue capacity, and backend idempotency must be considered together.

## 11. Immediate retry policy

Immediate retry is allowed only when:

- the error is explicitly classified as short-lived;
- the operation is idempotent or deduplicated;
- the previous transaction is fully cleaned up;
- the immediate-retry budget remains;
- retry will not repeat an authentication or configuration failure;
- and the outer deadline still permits useful completion.

Immediate retry shall not be used for:

- invalid payload/schema;
- authorization denial;
- invalid server certificate;
- revoked/missing credential;
- unsupported version;
- HTTP `4xx` unless the specific status has a documented retry rule;
- repeated modem/network unavailability;
- queue/resource exhaustion without a state change.

## 12. Backoff policy

### 12.1 Exponential backoff

The proposed calculation is:

```text
raw_delay = min(base_delay * 2^retry_index, max_delay)
delay = apply_bounded_jitter(raw_delay)
next_deadline = now_monotonic + delay
```

Arithmetic shall saturate and avoid overflow.

### 12.2 Proposed baseline

| Parameter | Normal records | High/critical records |
|---|---:|---:|
| Base delay | 5 s | 2 s |
| Maximum delay | 15 min | 60 s initially, then policy may degrade to normal cap |
| Jitter range | ±20% | ±20% |

Critical priority shall not create continuous radio/modem activity during a long outage. After a bounded urgent window, the record follows normal outage backoff while retaining priority.

### 12.3 Jitter

Production jitter reduces synchronized fleet reconnects. Requirements:

- jitter is bounded and never produces a negative delay;
- security-sensitive random material is not required for ordinary backoff jitter;
- Linux simulation uses an explicit deterministic seed or injected jitter provider;
- repeated replay with the same seed produces byte-identical trace timing;
- unit tests cover lower/upper jitter bounds.

## 13. Channel health and circuit state

MQTT and HTTP health are tracked independently even though both depend on the same cellular modem/network.

### 13.1 Proposed channel states

```text
DISABLED
IDLE
PROBING
HEALTHY
DEGRADED
OPEN
RECOVERING
SECURITY_BLOCKED
CONFIG_BLOCKED
```

### 13.2 Failure attribution

| Failure | Penalizes |
|---|---|
| Invalid telemetry record | Record only; not channel health |
| HTTP unsupported schema | Record/API compatibility; may mark HTTP config incompatible |
| MQTT broker unreachable | MQTT channel |
| HTTP server `503` | HTTP channel |
| Cellular registration lost | Shared network dependency; both remote channels unavailable |
| TLS trust failure for one endpoint | Affected endpoint/channel security state |
| Device credential rejected by shared identity service | Security state; potentially both channels |
| Modem parser/reset failure | Shared modem component state |

### 13.3 Circuit opening

A channel may enter `OPEN` after a threshold of consecutive attributable failures. While open:

- new normal attempts are not started;
- one scheduled probe may be allowed after the open interval;
- queue records remain owned by the delivery service;
- another eligible channel may be selected by delivery policy;
- success closes or degrades the circuit according to recovery policy.

Thresholds and open intervals are finalized in `05_remote_delivery_policy.md`.

## 14. Success and counter reset

Not every success resets every counter.

| Success | Counters/state eligible to reset |
|---|---|
| Modem ready | Modem boot/recovery counter only |
| Network registered | Network-registration counter |
| MQTT connected | MQTT connection counter, not record delivery counter |
| HTTP response received | HTTP transport timeout counter, subject to status classification |
| Record accepted | Record attempt state and relevant channel success streak |
| Security re-provisioned | Security-blocked state after validation |

One successful probe should not immediately erase evidence of a prolonged outage if recovery policy requires a stable-health window.

## 15. Record lifetime and expiration

Each queued record has:

- `created_at` metadata;
- monotonic creation/restore metadata where available;
- `expires_after_ms` or record-class policy;
- attempt count and last result;
- stable `record_id`.

### 15.1 Expiration rules

1. Expiration is evaluated before starting an attempt and after recovery from a long operation.
2. In-flight completion may be accepted after the nominal lifetime only if the attempt began while eligible and policy explicitly allows it.
3. Expired records are not silently reported as delivered.
4. Expiration produces a bounded diagnostic/counter according to record class.
5. Expired telemetry may be dropped after accounting; critical event loss may require persistent audit state.
6. Wall-clock corrections shall not unexpectedly resurrect expired records; monotonic/recovery policy governs local lifetime.

Exact lifetime per record class belongs to the common data and remote-delivery policy.

## 16. Delivery queue overflow

Queue capacity is bounded at build/configuration time.

### 16.1 Prohibited behavior

- Unbounded dynamic growth.
- Overwriting an in-flight/reserved record.
- Dropping a record without counters/diagnostics required by its class.
- Dropping a high-priority record to admit a lower-priority record.
- Encoding transport-specific packets as the only queued representation.

### 16.2 Proposed overflow order

When full, evaluate in this order:

1. Reject malformed/new record before queue consideration.
2. Coalesce an explicitly coalescible record class.
3. Remove expired records.
4. Evict the oldest lowest-priority eligible record if policy permits loss.
5. Reject the new record if no allowed eviction exists.
6. Raise a bounded queue-overflow diagnostic/counter.

### 16.3 Coalescing

Potentially coalescible:

- periodic device status;
- repeated identical diagnostics with occurrence counter;
- scheduled telemetry only when product retention policy permits latest-only behavior.

Not coalescible by default:

- command results;
- configuration results;
- confirmed leak transition events;
- security events requiring audit;
- records already reserved/in flight.

## 17. Priority and fairness

Priority classes are defined by the common data contract.

Requirements:

- higher priority is selected first among eligible records;
- one critical record shall not permanently starve normal telemetry;
- repeated high-priority failures do not create an infinite head-of-line block;
- adapters may skip a record temporarily if it is incompatible with the currently available channel, while preserving deterministic order rules;
- HTTP batching shall not mix records when priority/lifetime/result semantics become ambiguous.

A proposed fairness mechanism is a bounded high-priority burst followed by one eligible normal record. Exact burst size remains open.

## 18. Cross-channel fallback boundary

This document defines fallback safety requirements; `05_remote_delivery_policy.md` chooses when fallback occurs.

Fallback is allowed only when:

- the same semantic record is supported on the target channel;
- stable `record_id` and immutable payload are preserved;
- previous attempt cleanup is complete;
- target channel security/configuration is valid;
- target channel circuit is eligible;
- retry and record-lifetime budgets permit it;
- server-side/device-side deduplication is defined.

Fallback shall not occur for:

- invalid or unsupported record semantic schema on both channels;
- authentication failure caused by a shared revoked identity unless target uses an independently valid credential path;
- expired record;
- configuration that explicitly disables target channel;
- non-idempotent operation without a cross-channel idempotency contract.

## 19. Duplicate delivery and lost acknowledgement

The ambiguous case is:

```text
remote service accepts record
    -> response/acknowledgement is lost
    -> device times out
```

Required behavior:

- classify as delivery outcome unknown/retryable only where idempotency exists;
- retry with the same `(device_id, record_id)`;
- do not rebuild payload or timestamp;
- remote service returns accepted/already-accepted semantics consistently;
- record with same identity but different payload is a protocol violation;
- diagnostics distinguish duplicate acceptance from first acceptance when evidence exists.

## 20. MQTT result mapping

The MQTT document shall refine this table.

| MQTT result | Default normalized classification |
|---|---|
| Connection refused: bad credentials/not authorized | `SECURITY_RECOVERY_REQUIRED` |
| Connection refused: unavailable/server busy | `RETRYABLE_NEW_SESSION` |
| Network disconnect | `RETRYABLE_NEW_NETWORK` or session recovery based on modem state |
| Publish QoS 0 send accepted | `ACK_ONLY` unless contract defines send completion as sufficient |
| QoS 1 PUBACK success | `ACK_ONLY` or `SUCCESS` according to application-receipt policy |
| QoS 2 complete | Transport success; application acceptance still contract-dependent |
| Publish timeout | `RETRYABLE_NEW_SESSION` or delivery outcome unknown |
| Unsupported topic/payload contract | `PERMANENT_RECORD_FAILURE` or `CONFIGURATION_REQUIRED` |
| Application receipt rejects schema | `PERMANENT_RECORD_FAILURE` |
| Broker rate/limit response where available | `RETRYABLE_NEW_SESSION` with bounded server hint |

MQTT packet identifiers are attempt/session identifiers and do not replace `record_id`.

## 21. HTTP result mapping

The HTTP API document may override only with endpoint-specific justification.

| Result | Default classification |
|---|---|
| `200`, `201`, `202`, `204` | Success only according to endpoint acceptance semantics |
| `400` | Permanent record/request failure |
| `401` | Security recovery required; token refresh only if explicitly supported |
| `403` | Security/authorization failure; no normal retry |
| `404` | Configuration/API-version failure unless endpoint contract says transient |
| `408` | Retryable; outcome may be ambiguous |
| `409` | Interpret via idempotency/conflict contract; may mean already accepted or permanent conflict |
| `413` | Permanent batch/request-size failure; batch may be deterministically split if allowed |
| `415` | Unsupported media/schema mapping; no unchanged retry |
| `422` | Permanent semantic validation failure |
| `425` | Retry later without unsafe early data |
| `429` | Retryable with bounded `Retry-After` handling |
| `500`, `502`, `503`, `504` | Retryable with backoff unless repeated policy opens channel |
| TLS validation failure | Security recovery required; fail closed |
| DNS/network/connection timeout | Retryable network/session failure |
| Response parse/size failure | Protocol failure; retry only if idempotent and bounded |

### 21.1 `Retry-After`

- Validate syntax and bound the value.
- Convert to monotonic deadline.
- Do not accept a remote delay beyond record lifetime without expiring/accounting the record.
- Apply local minimum/maximum policy.
- Invalid value falls back to local backoff.

### 21.2 Partial batch success

HTTP partial success shall yield one result per `record_id`:

- accepted records complete;
- retryable records return to queue with attempt metadata;
- permanently rejected records terminate individually;
- missing/ambiguous result entries use the endpoint's defined safe classification;
- the whole batch is not retried blindly after known partial acceptance.

## 22. Modem and cellular recovery

Recovery stages should escalate rather than reset immediately:

```text
verify parser/transaction state
  -> close affected protocol session
  -> recheck data context
  -> recheck network registration
  -> reinitialize modem service
  -> controlled modem reset
  -> power-cycle only if hardware contract permits
  -> security/configuration/service-required state
```

Requirements:

- one owner performs recovery;
- repeated resets are rate-limited;
- stale AT responses/URCs cannot complete a new attempt;
- recovery has an overall deadline;
- every stage releases or renews the correct power lease;
- measurement continues during recovery;
- after reset, MQTT and HTTP session state is rebuilt as disconnected.

## 23. STM32–nRF52810 retry policy

### 23.1 Frame and command errors

| Error | Default behavior |
|---|---|
| Frame CRC/integrity failure | Drop/NACK according to frame contract; bounded diagnostic |
| Unknown message/version | Reject unsupported; no state mutation |
| ACK/response timeout | One bounded retry if command is idempotent/deduplicated |
| Link reset/session change | Cancel old attempts and perform handshake |
| Reassembly timeout | Discard partial object and release buffer |
| Repeated parser desync | Reset link parser/nRF interface according to recovery budget |

State-changing BLE commands shall carry application transaction identity so transport retry does not repeat the product effect.

## 24. BLE application timeout and retry

- Mobile-side retry shall reuse the command/request identity for the same operation.
- nRF52810 shall not invent a new STM32 command after losing the BLE response.
- STM32 command result may be cached for a bounded duplicate window.
- Disconnect cancels volatile authorization/session state.
- Persistent commit may continue only if ownership and final result recovery are defined; otherwise it is safely aborted/recovered by storage semantics.
- Pairing/authentication failures follow security backoff, not ordinary command retry.

## 25. Security error policy

Security errors are not ordinary network errors.

| Error | Retry behavior |
|---|---|
| Invalid server certificate/hostname | No normal retry until trust/time/config state changes |
| Missing/revoked credential | Stop normal channel retries; provisioning/service recovery |
| Authentication rejected | Maximum bounded confirmation attempts, then security backoff |
| Authorization denied | No unchanged retry |
| Replay detected | No execution; return known/bounded duplicate result |
| Security version downgrade | Reject; no fallback to insecure profile |
| Trusted time unavailable | Follow explicit bootstrap policy; never disable validation silently |

## 26. Configuration error policy

Configuration errors remain blocked until relevant state changes:

- missing endpoint/broker/APN profile;
- unsupported channel selection;
- invalid reporting/retry range;
- missing credential reference;
- incompatible protocol/schema version;
- storage validation failure.

The service should record the configuration revision that caused blocking. It may retry validation when that revision changes, rather than on a periodic network retry loop.

## 27. Low-power interaction

### 27.1 Sleep eligibility

The system may sleep while records are queued if:

- no transaction owns an active power lease;
- next retry/report/keep-alive deadline is scheduled;
- adapter and driver buffers are in a sleep-safe state;
- modem shutdown/retention policy is satisfied;
- record priority does not require an earlier bounded attempt.

### 27.2 Prohibited outage behavior

- continuous connection attempts without backoff;
- holding modem power indefinitely because queue is non-empty;
- repeatedly waking before the calculated deadline;
- using wall clock instead of monotonic deadline for local retry;
- extending the retry deadline on every idle event.

## 28. Scheduler and event-loop behavior

On retry scheduling:

1. Calculate the next absolute monotonic deadline once.
2. Store it in the owning service state.
3. Schedule or derive one retry-due event.
4. Ignore/collapse duplicate retry-due events for the same generation.
5. At deadline, re-evaluate eligibility before starting work.
6. Start at most one bounded state transition/operation per service turn as required by runtime budget.

Error handling shall publish bounded events rather than recursively calling recovery chains.

## 29. Error-code namespace

Normalized communication errors should use stable categories:

```text
COMM-COMMON-xxx
COMM-QUEUE-xxx
COMM-DELIVERY-xxx
COMM-MQTT-xxx
COMM-HTTP-xxx
COMM-BLE-xxx
COMM-NRF-xxx
COMM-MODEM-xxx
COMM-NETWORK-xxx
COMM-SECURITY-xxx
COMM-CONFIG-xxx
```

Each error entry shall define:

- stable code and symbolic name;
- scope and severity;
- transient/persistent/permanent classification;
- retry classification;
- channel-health impact;
- diagnostic context fields;
- security/redaction constraints;
- recovery owner.

Vendor codes may be attached as bounded context but shall not replace normalized product codes.

## 30. Diagnostics and counters

Minimum counters/state include:

- attempts and successes per channel;
- timeout count by operation class;
- retry count;
- fallback count and direction;
- channel circuit open/probe/recovery count;
- authentication/authorization failure count;
- modem reset/recovery count;
- queue high-water mark;
- overflow, eviction, coalescing, and expiration count by priority/type;
- duplicate/already-accepted count;
- stale completion count;
- last normalized error per channel/component.

Counters saturate. Diagnostics shall not contain credentials or unredacted payloads.

## 31. Reboot recovery

After reboot:

1. Restore only valid persistent queue/metadata records.
2. Treat all protocol sessions and attempts as inactive/disconnected.
3. Preserve `record_id` for recovered records.
4. Convert previously in-flight records to outcome-unknown/retryable only if idempotency permits.
5. Apply startup delay/jitter to avoid fleet synchronization where required.
6. Re-evaluate expiration, configuration, security, and channel eligibility.
7. Do not restore stale power leases or volatile protocol identifiers.

## 32. Deterministic Linux simulation

Simulation shall inject:

- completion before, exactly at, and after deadline;
- lost completion and duplicate late completion;
- retryable then successful operation;
- repeated failure until circuit opens;
- probe failure and probe success;
- MQTT failure followed by HTTP fallback;
- HTTP failure followed by MQTT fallback where allowed;
- lost acknowledgement after remote acceptance;
- partial HTTP batch success;
- queue overflow and priority eviction;
- expiration during backoff and during recovery;
- modem reset budget exhaustion;
- security/configuration blocked states;
- reboot with recovered in-flight record;
- deterministic jitter using known seed.

Repeated runs with the same scenario and seed shall produce byte-identical normalized traces.

## 33. Hardware validation

Measure and record on STM32/EC200U/nRF hardware:

- modem boot-ready distribution;
- network registration and data-context distribution under normal/weak/no network;
- MQTT connect and publish result latency;
- HTTP single and maximum-batch latency;
- TLS handshake and session-reuse latency;
- nRF command and BLE end-to-end latency;
- stale URC/response behavior after cancel/reset;
- modem recovery and reset timing;
- current consumption during retries/backoff;
- queue/memory pressure at maximum payload;
- server `Retry-After`, partial acceptance, duplicate receipt, and lost-response behavior.

Proposed timeout values shall be changed only with documented evidence and updated tests.

## 34. Acceptance test matrix

| Scenario | Expected outcome |
|---|---|
| One transient timeout | Bounded retry with same record identity |
| Repeated channel failure | Backoff, health degradation, circuit open |
| Invalid payload | Permanent record failure; channel remains usable |
| Shared network loss | MQTT and HTTP unavailable; one modem recovery owner |
| HTTP `401/403` | Security recovery/block; no normal retry loop |
| HTTP `429` | Bounded server hint plus local policy |
| HTTP partial batch | Per-record completion/retry/failure |
| MQTT ACK lost | Idempotent same-record retry according to receipt policy |
| Queue full | Deterministic coalesce/expire/evict/reject and diagnostic |
| Critical event during outage | Priority retained, urgent window bounded, then normal backoff |
| Completion after timeout | Stale completion ignored without corrupting newer attempt |
| Reboot during in-flight delivery | Session cleared; record identity preserved; safe retry if allowed |
| Long outage | Measurement continues and low-power backoff remains bounded |

## 35. Initial proposed decisions

| ID | Proposed decision |
|---|---|
| `COMM-REL-001` | Use absolute monotonic deadlines for all local timeout and backoff decisions |
| `COMM-REL-002` | Permit at most one immediate retry for explicitly safe transient cases |
| `COMM-REL-003` | Use bounded exponential backoff with ±20% jitter |
| `COMM-REL-004` | Track MQTT and HTTP health independently while sharing modem/network dependency state |
| `COMM-REL-005` | Preserve stable `record_id` and immutable payload across all retries/fallback |
| `COMM-REL-006` | Treat invalid payload as record failure, not automatic channel failure |
| `COMM-REL-007` | Stop normal retries for security/configuration-blocked errors until relevant state changes |
| `COMM-REL-008` | Store transport-neutral records in a bounded delivery queue |
| `COMM-REL-009` | Use deterministic coalesce/expire/priority-evict/reject order on overflow |
| `COMM-REL-010` | Escalate modem recovery in stages with bounded reset budget |
| `COMM-REL-011` | Require per-record HTTP batch results and safe duplicate handling |
| `COMM-REL-012` | Allow sleep during backoff when no active operation owns a power lease |

## 36. Open decisions

| Decision | Question | Blocking impact |
|---|---|---|
| Record lifetimes | Lifetime per telemetry/event/status/diagnostic/result class | Queue and expiration tests |
| Maximum record attempts | Count and elapsed-time limit per class | Terminal-failure/loss policy |
| Queue capacity/persistence | RAM count, F-RAM critical subset, or bounded persistent queue | Storage allocation and reboot recovery |
| Overflow policy | Which record classes may coalesce or be evicted | Data-retention requirements |
| Fairness burst | Maximum consecutive high-priority records | Queue scheduler |
| Circuit threshold | Failure count/window/open duration/probe rule | Remote delivery policy |
| Primary/fallback trigger | Which error classes and thresholds switch channel | `05_remote_delivery_policy.md` |
| Recovery-to-primary | Stable-health window and duplicate protection | Delivery policy |
| MQTT acceptance | QoS ACK versus application receipt per record type | MQTT message catalog |
| HTTP endpoint semantics | Success codes, `409`, partial response, idempotency | HTTP API/delivery documents |
| Modem budgets | Reset counts, registration interval, power-cycle permission | Hardware/power integration |
| Final timeout constants | Hardware-measured values and margins | All protocol implementations |

## 37. Definition of ready

This document may move from `Proposed` to `Accepted` when:

- error taxonomy and normalized result codes have a normative catalog owner;
- record lifetime, maximum attempts, and queue overflow rules are accepted;
- `05_remote_delivery_policy.md` defines channel switching and recovery-to-primary;
- MQTT application acceptance semantics are selected;
- HTTP status, idempotency, and partial-batch response semantics are selected;
- proposed timeout values are validated or marked explicitly as simulation-only defaults;
- modem recovery/reset and power constraints are reviewed against hardware;
- security/configuration-blocked recovery paths are owned;
- deterministic Linux scenarios cover every terminal and retry transition;
- and no open decision can produce an unbounded retry, queue, wake, or duplicate-delivery path.
