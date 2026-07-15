# Common Data Contract

> **Document ID:** COMM-DATA-001
> **Status:** Draft
> **Baseline:** Transport-neutral application records shared by MQTT, HTTP, BLE, and internal links
> **Applies to:** STM32 firmware, Linux simulation, nRF52810 firmware, mobile application, and remote services

## 1. Purpose

This document defines the protocol-independent communication data model of the Smart Water Flow and Pressure Monitor.

It specifies:

* canonical record types and common metadata;
* device and record identity;
* timestamps and time-quality representation;
* measurement values, units, scaling, ranges, and validity;
* telemetry, event, status, diagnostic, command, configuration, and delivery-result models;
* rules for optional, unavailable, invalid, stale, and unknown data;
* validation and compatibility requirements;
* and mapping boundaries for MQTT, HTTP, BLE, and internal transport documents.

This contract defines semantic data. It does not define MQTT topics, HTTP endpoints, BLE UUIDs, byte-level frames, authentication mechanisms, or final JSON/CBOR/binary syntax.

## 2. Design goals

The common data contract shall:

1. preserve the same meaning across MQTT and HTTP;
2. permit deterministic encoding and decoding on Linux and STM32;
3. avoid dependence on compiler layout, ABI, endianness, or floating-point formatting;
4. preserve measurement quality and time quality;
5. support stable record identity and cross-channel deduplication;
6. support forward-compatible optional fields;
7. make malformed and unsupported input rejectable before domain mutation;
8. remain bounded for embedded memory and cellular payload constraints;
9. distinguish transport delivery from application acceptance;
10. avoid exposing secrets or internal pointer/state representations.

## 3. Normative terminology

The keywords **shall**, **shall not**, **should**, **should not**, and **may** express requirement strength.

| Term             | Meaning                                                                            |
| ---------------- | ---------------------------------------------------------------------------------- |
| Record           | Immutable application-level communication object with one stable identity          |
| Payload          | Record-specific semantic fields excluding transport envelope                       |
| Envelope         | Common metadata plus one typed payload                                             |
| Producer         | Component that creates a record                                                    |
| Consumer         | Component that validates and processes a record                                    |
| Available        | A value is present and may be interpreted with its quality flags                   |
| Valid            | A value passed the domain validity checks required for its use                     |
| Invalid          | A value exists but failed one or more validity checks                              |
| Stale            | A previously valid value is older than its allowed freshness window                |
| Unknown          | The producer cannot determine the value or state                                   |
| Unsupported      | The consumer recognizes the category but does not implement its version or subtype |
| Delivery success | Remote acceptance according to the protocol/application contract                   |
| Command success  | The requested operation completed according to product semantics                   |

## 4. Separation of semantic model and wire encoding

This document defines logical types such as `uint32`, `int64`, Boolean, enum, bounded UTF-8 string, and bounded byte sequence.

Protocol documents shall map these types to their wire representation:

* MQTT may use JSON, CBOR, or another accepted payload format;
* HTTP may use the same semantic schema in JSON, CBOR, or another accepted media type;
* BLE and internal links may use compact binary mappings;
* Linux C/C++ structures may use native representations internally but shall encode field-by-field;
* STM32 structures shall not be transmitted using raw `memcpy()` unless a separate packed-wire contract explicitly defines layout, endianness, alignment, and versioning.

Changing the wire encoding does not permit changing field meaning, unit, scale, validity, or identity semantics.

## 5. Canonical primitive types

| Logical type | Meaning                                     | Wire requirement                                                  |
| ------------ | ------------------------------------------- | ----------------------------------------------------------------- |
| `bool`       | `true` or `false`                           | No alternative numeric meaning unless encoding explicitly maps it |
| `uint8`      | Unsigned 8-bit integer                      | Range 0–255                                                       |
| `uint16`     | Unsigned 16-bit integer                     | Range 0–65,535                                                    |
| `uint32`     | Unsigned 32-bit integer                     | Range 0–4,294,967,295                                             |
| `uint64`     | Unsigned 64-bit integer                     | Decimal JSON mapping shall preserve full integer precision        |
| `int16`      | Signed 16-bit integer                       | Two's-complement semantic range                                   |
| `int32`      | Signed 32-bit integer                       | Two's-complement semantic range                                   |
| `int64`      | Signed 64-bit integer                       | Decimal JSON mapping shall preserve full integer precision        |
| `enum`       | Named value with assigned numeric code      | Unknown codes shall not be silently mapped to a known value       |
| `flags32`    | Independent bit flags in a `uint32`         | Reserved bits shall be zero when produced                         |
| `string<N>`  | UTF-8 string with at most `N` encoded bytes | Shall not contain control characters unless explicitly allowed    |
| `bytes<N>`   | Byte sequence with at most `N` bytes        | Text encodings shall define base64/hex mapping                    |

Floating-point values may be used inside measurement algorithms, but canonical communication fields use integer values with explicit SI-derived units unless a later accepted decision states otherwise.

## 6. Naming conventions

### 6.1 Semantic names

* Field names use `snake_case` in documentation and text encodings.
* C identifiers may follow repository naming conventions but shall map one-to-one to semantic names.
* Unit suffixes are included when needed to prevent ambiguity, for example `pressure_pa` and `temperature_mdeg_c`.
* Boolean fields use positive meaning where possible, for example `time_valid` rather than `time_invalid`.
* Enumerations use stable symbolic names and numeric codes.

### 6.2 Reserved names

The following names are common metadata and shall not be redefined inside payloads:

```text
schema_version
record_type
record_id
device_id
created_at_unix_ms
time_quality
priority
expires_after_ms
payload
```

## 7. Device identity

### 7.1 `DeviceIdentity`

| Field                    | Type         |    Required | Meaning                                              |
| ------------------------ | ------------ | ----------: | ---------------------------------------------------- |
| `device_id`              | `string<48>` |         Yes | Stable product identity used by application services |
| `serial_number`          | `string<32>` |         Yes | Human-readable manufacturing serial number           |
| `product_model`          | `string<32>` |         Yes | Product family/model identifier                      |
| `hardware_revision`      | `string<16>` |         Yes | Hardware revision                                    |
| `firmware_version`       | `string<32>` |         Yes | Main STM32 firmware semantic/build version           |
| `firmware_variant`       | `string<32>` |         Yes | Compiled product/sensor variant                      |
| `sensor_profile_id`      | `string<32>` | Conditional | Pressure/measurement sensor profile identifier       |
| `sensor_profile_version` | `uint32`     | Conditional | Sensor-profile schema/content version                |
| `ble_firmware_version`   | `string<32>` |    Optional | nRF52810 firmware version when available             |
| `modem_firmware_version` | `string<48>` |    Optional | EC200U-CN firmware version when available            |

### 7.2 Identity rules

1. `device_id` shall remain stable across reboot, firmware update, network change, and protocol change.
2. `device_id` shall not be derived from a secret credential.
3. MQTT client ID, HTTP credential ID, SIM identity, BLE address, and `device_id` are distinct concepts.
4. A protocol may use `device_id` in a topic or URL only if its security and privacy contract permits it.
5. Missing optional identity fields shall be omitted or marked unavailable according to encoding rules; empty strings shall not mean unknown unless explicitly specified.

## 8. Record identity

### 8.1 `record_id`

`record_id` is a device-scoped, stable `uint64` identifier assigned once when a record is created.

The remote deduplication key is:

```text
(device_id, record_id)
```

### 8.2 Record-identity rules

1. A record shall retain the same `record_id` across retry, reconnect, reboot recovery, MQTT/HTTP fallback, and retransmission.
2. A modified payload is a new record and shall receive a new `record_id`.
3. A protocol-specific packet identifier, MQTT Packet Identifier, HTTP request ID, UART sequence number, or BLE transaction ID shall not replace `record_id`.
4. `record_id = 0` is reserved as invalid/unassigned.
5. A device shall not intentionally reuse a valid `record_id` while the server's deduplication window may still contain the earlier record.
6. Exact ID generation and persistence are implementation decisions, but uniqueness across reboot shall be demonstrated.

### 8.3 Proposed generation strategy

A suitable embedded strategy is to compose a persistent boot/session epoch and a per-session sequence into a `uint64`. The exact bit allocation remains open until persistence and lifetime analysis are completed.

## 9. Time model

### 9.1 Time fields

| Field                | Type     | Unit                    | Meaning                                                                           |
| -------------------- | -------- | ----------------------- | --------------------------------------------------------------------------------- |
| `created_at_unix_ms` | `uint64` | ms since Unix epoch UTC | Wall-clock creation time when valid                                               |
| `sampled_at_unix_ms` | `uint64` | ms since Unix epoch UTC | Measurement sampling/publication time when valid                                  |
| `uptime_ms`          | `uint64` | ms                      | Monotonic time since current boot/session                                         |
| `age_ms`             | `uint32` | ms                      | Age of referenced data at record creation, saturated at `UINT32_MAX`              |
| `expires_after_ms`   | `uint32` | ms                      | Record lifetime relative to creation; zero means policy-defined/no explicit value |

### 9.2 `TimeQuality`

| Code | Name                  | Meaning                                                                              |
| ---: | --------------------- | ------------------------------------------------------------------------------------ |
|    0 | `TIME_UNKNOWN`        | Wall-clock time unavailable or not trusted                                           |
|    1 | `TIME_RTC_UNVERIFIED` | RTC value exists but has not been verified against an accepted source                |
|    2 | `TIME_SYNCHRONIZED`   | Time synchronized within the accepted product tolerance                              |
|    3 | `TIME_HOLDOVER`       | Previously synchronized time is being maintained without current source confirmation |

### 9.3 Time rules

1. Monotonic scheduling shall not depend on wall-clock time.
2. `created_at_unix_ms = 0` shall mean unavailable only when `time_quality = TIME_UNKNOWN`.
3. Consumers shall inspect `time_quality` before using wall-clock fields for billing, ordering, or audit purposes.
4. Records created without trusted wall-clock time still require stable `record_id` and monotonic metadata where available.
5. Time synchronization shall not rewrite the identity or original sample semantics of an already-created record.
6. Protocol adapters shall not generate a new measurement timestamp during transmission.

## 10. Common record envelope

### 10.1 `CommonRecordEnvelope`

| Field                | Type             | Required | Meaning                                  |
| -------------------- | ---------------- | -------: | ---------------------------------------- |
| `schema_version`     | `uint16`         |      Yes | Version of the common semantic schema    |
| `record_type`        | `RecordType`     |      Yes | Payload discriminator                    |
| `record_id`          | `uint64`         |      Yes | Stable device-scoped record identity     |
| `device_id`          | `string<48>`     |      Yes | Producer identity                        |
| `created_at_unix_ms` | `uint64`         |      Yes | Creation time or zero when unavailable   |
| `uptime_ms`          | `uint64`         |      Yes | Monotonic creation time for current boot |
| `time_quality`       | `TimeQuality`    |      Yes | Validity of wall-clock fields            |
| `priority`           | `RecordPriority` |      Yes | Delivery priority class                  |
| `expires_after_ms`   | `uint32`         |      Yes | Lifetime policy input                    |
| `payload`            | typed object     |      Yes | Object selected by `record_type`         |

### 10.2 `RecordType`

|    Code | Name                          | Payload                    |
| ------: | ----------------------------- | -------------------------- |
|       1 | `RECORD_TELEMETRY`            | `TelemetryRecord`          |
|       2 | `RECORD_EVENT`                | `EventRecord`              |
|       3 | `RECORD_DEVICE_STATUS`        | `DeviceStatusRecord`       |
|       4 | `RECORD_DIAGNOSTIC`           | `DiagnosticRecord`         |
|       5 | `RECORD_COMMAND_RESULT`       | `CommandResult`            |
|       6 | `RECORD_CONFIGURATION_RESULT` | `ConfigurationResult`      |
|   7–127 | Reserved core types           | —                          |
| 128–255 | Future/vendor extension range | Requires explicit contract |

### 10.3 `RecordPriority`

| Code | Name                  | Intended use                                                            |
| ---: | --------------------- | ----------------------------------------------------------------------- |
|    0 | `PRIORITY_BACKGROUND` | Non-urgent diagnostic or maintenance data                               |
|    1 | `PRIORITY_NORMAL`     | Scheduled telemetry and ordinary status                                 |
|    2 | `PRIORITY_HIGH`       | Important events and degraded-health reports                            |
|    3 | `PRIORITY_CRITICAL`   | Explicitly approved critical events requiring earliest allowed delivery |

Priority affects queue selection and permitted wake/delivery timing. It shall not bypass authentication, retry bounds, payload validation, or power-safety constraints.

## 11. Measurement quality

### 11.1 Common quality representation

Each measurement value is accompanied by a `quality_flags` field. A record-level aggregate may summarize the component flags but shall not hide them.

### 11.2 `MeasurementQualityFlags`

|   Bit | Name                                 | Meaning                                                         |
| ----: | ------------------------------------ | --------------------------------------------------------------- |
|     0 | `QUALITY_VALID`                      | Value is valid for its defined production use                   |
|     1 | `QUALITY_UNAVAILABLE`                | No current value is available                                   |
|     2 | `QUALITY_INVALID`                    | Value exists but failed validation                              |
|     3 | `QUALITY_STALE`                      | Value exceeded its freshness limit                              |
|     4 | `QUALITY_SENSOR_FAULT`               | Source sensor or front-end reported a fault                     |
|     5 | `QUALITY_TIMEOUT`                    | Measurement or acquisition timed out                            |
|     6 | `QUALITY_OUT_OF_RANGE`               | Value is outside the accepted configured range                  |
|     7 | `QUALITY_CALIBRATION_MISSING`        | Required calibration/profile was unavailable                    |
|     8 | `QUALITY_CALIBRATION_DEGRADED`       | Fallback or degraded calibration was used                       |
|     9 | `QUALITY_TIME_UNCERTAIN`             | Sampling time is unavailable or insufficiently trusted          |
|    10 | `QUALITY_ESTIMATED`                  | Value was estimated rather than directly measured               |
|    11 | `QUALITY_REVERSED`                   | Direction/sign indicates accepted reverse flow where applicable |
| 12–23 | Reserved common flags                | Producer shall set to zero                                      |
| 24–31 | Measurement-specific extension flags | Defined by owning measurement document                          |

### 11.3 Quality invariants

1. `QUALITY_VALID` shall not be combined with `QUALITY_UNAVAILABLE` or `QUALITY_INVALID`.
2. `QUALITY_STALE` may accompany a retained numeric value but prevents use where fresh production data is required.
3. A missing numeric value shall be represented by absence plus `QUALITY_UNAVAILABLE`; magic numeric sentinels shall not be used on the semantic layer.
4. Zero is a legitimate measurement value and shall not mean missing.
5. Protocol mappings shall preserve all known flags.
6. Unknown extension flags shall be retained or reported as unsupported where the encoding permits; they shall not be silently relabeled.

## 12. Canonical measurement values

### 12.1 Fixed-point and unit policy

Communication fields use explicit integer units:

| Quantity             | Field                     | Type     | Unit/resolution                            |
| -------------------- | ------------------------- | -------- | ------------------------------------------ |
| Temperature          | `temperature_mdeg_c`      | `int32`  | 0.001 °C                                   |
| Flow rate            | `flow_rate_ml_per_h`      | `int64`  | 1 mL/h; sign represents accepted direction |
| Pressure             | `pressure_pa`             | `int32`  | 1 Pa                                       |
| Total forward volume | `total_forward_volume_ml` | `uint64` | 1 mL                                       |
| Total reverse volume | `total_reverse_volume_ml` | `uint64` | 1 mL                                       |
| Net volume           | `net_volume_ml`           | `int64`  | 1 mL                                       |
| Battery voltage      | `battery_mv`              | `uint16` | 1 mV                                       |
| Signal strength      | `signal_rssi_dbm`         | `int16`  | 1 dBm                                      |

Domain documents remain normative for production validation ranges. This contract defines storage capacity and units, not sensor capability claims.

### 12.2 Conversion rules

1. Internal floating-point values shall be rounded using one documented project-wide rule before communication encoding.
2. Conversion overflow shall set an error/quality condition; values shall not wrap.
3. Saturation may be used only when explicitly defined and shall set `QUALITY_OUT_OF_RANGE` or a diagnostic flag.
4. Unit conversions shall occur once at the domain-to-record boundary.
5. MQTT and HTTP mappings shall use the same logical integer values.

## 13. `MeasurementValue` pattern

Each measurement component follows this logical pattern:

| Field                | Type                      |    Required | Meaning                                           |
| -------------------- | ------------------------- | ----------: | ------------------------------------------------- |
| `value`              | quantity-specific integer | Conditional | Present when a numeric value exists               |
| `quality_flags`      | `flags32`                 |         Yes | Validity and diagnostic quality                   |
| `age_ms`             | `uint32`                  |         Yes | Age at record creation                            |
| `sampled_at_unix_ms` | `uint64`                  |    Optional | Original sample time when wall clock is available |

Protocol-specific schemas may flatten `value` into `pressure_pa`, `flow_rate_ml_per_h`, or another named field, but quality and age shall remain associated unambiguously.

## 14. `TelemetryRecord`

### 14.1 Purpose

`TelemetryRecord` carries a stable snapshot of production measurements and accumulated values for scheduled or policy-requested remote delivery.

### 14.2 Fields

| Field                     | Type               | Required | Meaning                                               |
| ------------------------- | ------------------ | -------: | ----------------------------------------------------- |
| `snapshot_sequence`       | `uint64`           |      Yes | Sequence of the published `RuntimeSnapshot`           |
| `snapshot_quality_flags`  | `flags32`          |      Yes | Aggregate snapshot quality                            |
| `temperature`             | measurement object |      Yes | Temperature value and quality                         |
| `flow_rate`               | measurement object |      Yes | Signed flow-rate value and quality                    |
| `pressure`                | measurement object |      Yes | Pressure value and quality                            |
| `total_forward_volume_ml` | `uint64`           |      Yes | Accepted accumulated forward volume                   |
| `total_reverse_volume_ml` | `uint64`           | Optional | Accepted accumulated reverse volume when supported    |
| `net_volume_ml`           | `int64`            | Optional | Net accumulated volume when product policy exposes it |
| `leak_state`              | `LeakState`        |      Yes | Current leak-detection state                          |
| `leak_flags`              | `flags32`          |      Yes | Leak evidence/status flags owned by leak contract     |
| `battery_mv`              | `uint16`           | Optional | Battery voltage when available                        |
| `device_health`           | `DeviceHealth`     |      Yes | Aggregate device-health state                         |
| `config_revision`         | `uint32`           |      Yes | Active runtime configuration revision                 |
| `calibration_revision`    | `uint32`           |      Yes | Active calibration revision                           |

### 14.3 `LeakState`

| Code | Name              | Meaning                                                         |
| ---: | ----------------- | --------------------------------------------------------------- |
|    0 | `LEAK_UNKNOWN`    | Leak state cannot be evaluated reliably                         |
|    1 | `LEAK_CLEAR`      | No active leak condition according to current policy            |
|    2 | `LEAK_SUSPECTED`  | Evidence threshold for suspected leak reached                   |
|    3 | `LEAK_CONFIRMED`  | Confirmed leak condition according to accepted detection policy |
|    4 | `LEAK_RECOVERING` | Leak evidence is clearing but recovery criteria are incomplete  |

The leak-detection document is normative for transitions and evidence. Communication shall not recompute leak state.

### 14.4 Telemetry rules

1. All fields shall originate from the same published snapshot or explicitly carry their own age/quality.
2. Invalid or stale values may be reported for diagnostics but shall not be marked valid.
3. Communication shall not update production volume.
4. Optional totals shall not be synthesized from rounded telemetry flow.
5. A retry uses the original record and timestamp, not a newly sampled snapshot.

## 15. `EventRecord`

### 15.1 Fields

| Field                       | Type                 |    Required | Meaning                                         |
| --------------------------- | -------------------- | ----------: | ----------------------------------------------- |
| `event_code`                | `uint32`             |         Yes | Stable product event identifier                 |
| `event_class`               | `EventClass`         |         Yes | Functional classification                       |
| `severity`                  | `Severity`           |         Yes | Event severity                                  |
| `state`                     | `EventState`         |         Yes | Raised, updated, cleared, or acknowledged state |
| `occurrence_count`          | `uint32`             |         Yes | Coalesced occurrence count, minimum 1           |
| `first_seen_unix_ms`        | `uint64`             | Conditional | First observed time when available              |
| `last_seen_unix_ms`         | `uint64`             | Conditional | Most recent observed time when available        |
| `related_snapshot_sequence` | `uint64`             |    Optional | Snapshot associated with the event              |
| `context`                   | bounded typed object |    Optional | Event-specific non-secret context               |

### 15.2 Enumerations

| Enum         | Values                                                                                            |
| ------------ | ------------------------------------------------------------------------------------------------- |
| `EventClass` | `MEASUREMENT`, `LEAK`, `STORAGE`, `POWER`, `COMMUNICATION`, `SECURITY`, `CONFIGURATION`, `SYSTEM` |
| `Severity`   | `INFO`, `NOTICE`, `WARNING`, `ERROR`, `CRITICAL`                                                  |
| `EventState` | `RAISED`, `UPDATED`, `CLEARED`, `ACKNOWLEDGED`                                                    |

### 15.3 Event rules

1. `event_code` meaning shall be defined in one event/diagnostic catalog.
2. A cleared event shall reference the same logical condition but receives its own communication `record_id`.
3. Repeated events may be coalesced only according to documented loss rules.
4. Context shall be bounded and shall not contain credentials, raw memory, or unrestricted vendor logs.

## 16. `DeviceStatusRecord`

| Field                     | Type            |    Required | Meaning                                                      |
| ------------------------- | --------------- | ----------: | ------------------------------------------------------------ |
| `device_health`           | `DeviceHealth`  |         Yes | Aggregate health                                             |
| `operating_state`         | `uint16` enum   |         Yes | Current application/FSM state mapped to stable external code |
| `uptime_ms`               | `uint64`        |         Yes | Current boot uptime                                          |
| `reset_reason`            | `uint16` enum   |         Yes | Stable normalized reset reason                               |
| `active_fault_count`      | `uint16`        |         Yes | Number of active reported faults                             |
| `delivery_queue_depth`    | `uint16`        |         Yes | Current queued remote records                                |
| `delivery_queue_capacity` | `uint16`        |         Yes | Configured queue capacity                                    |
| `preferred_channel`       | `RemoteChannel` |         Yes | Configured preferred remote channel                          |
| `active_channel`          | `RemoteChannel` | Conditional | Channel currently selected when applicable                   |
| `mqtt_state`              | `ChannelState`  |         Yes | Normalized MQTT state                                        |
| `http_state`              | `ChannelState`  |         Yes | Normalized HTTP state                                        |
| `cellular_state`          | `ChannelState`  |         Yes | Normalized modem/network state                               |
| `ble_state`               | `ChannelState`  |         Yes | Normalized local-link state                                  |
| `battery_mv`              | `uint16`        |    Optional | Battery voltage                                              |
| `signal_rssi_dbm`         | `int16`         |    Optional | Cellular signal strength                                     |
| `config_revision`         | `uint32`        |         Yes | Active configuration revision                                |
| `calibration_revision`    | `uint32`        |         Yes | Active calibration revision                                  |

### 16.1 `DeviceHealth`

| Code | Name              | Meaning                                            |
| ---: | ----------------- | -------------------------------------------------- |
|    0 | `HEALTH_UNKNOWN`  | Health cannot be evaluated                         |
|    1 | `HEALTH_OK`       | No known degraded or failed subsystem              |
|    2 | `HEALTH_DEGRADED` | Product remains operational with known degradation |
|    3 | `HEALTH_FAULT`    | One or more required capabilities failed           |

### 16.2 `RemoteChannel`

| Code | Name           |
| ---: | -------------- |
|    0 | `CHANNEL_NONE` |
|    1 | `CHANNEL_MQTT` |
|    2 | `CHANNEL_HTTP` |
|    3 | `CHANNEL_AUTO` |

`CHANNEL_AUTO` is configuration intent, not a physical transport. The delivery policy selects MQTT or HTTP.

### 16.3 `ChannelState`

| Code | Name                        | Meaning                                  |
| ---: | --------------------------- | ---------------------------------------- |
|    0 | `CHANNEL_STATE_UNKNOWN`     | State unavailable                        |
|    1 | `CHANNEL_STATE_DISABLED`    | Channel disabled by build/configuration  |
|    2 | `CHANNEL_STATE_IDLE`        | Available but no active transaction      |
|    3 | `CHANNEL_STATE_CONNECTING`  | Establishing prerequisite/session        |
|    4 | `CHANNEL_STATE_READY`       | Ready for an operation                   |
|    5 | `CHANNEL_STATE_ACTIVE`      | Operation in progress                    |
|    6 | `CHANNEL_STATE_BACKOFF`     | Retry blocked until deadline             |
|    7 | `CHANNEL_STATE_UNAVAILABLE` | Required network/service unavailable     |
|    8 | `CHANNEL_STATE_FAULT`       | Non-transient or recovery-required fault |

## 17. `DiagnosticRecord`

| Field                | Type                      |    Required | Meaning                                             |
| -------------------- | ------------------------- | ----------: | --------------------------------------------------- |
| `diagnostic_code`    | `uint32`                  |         Yes | Stable normalized diagnostic identifier             |
| `severity`           | `Severity`                |         Yes | Severity                                            |
| `subsystem`          | `Subsystem`               |         Yes | Owning subsystem                                    |
| `occurrence_count`   | `uint32`                  |         Yes | Number of matching occurrences                      |
| `first_seen_unix_ms` | `uint64`                  | Conditional | First occurrence time                               |
| `last_seen_unix_ms`  | `uint64`                  | Conditional | Last occurrence time                                |
| `vendor_code`        | `int32`                   |    Optional | Bounded vendor error code, not vendor text dump     |
| `context_u32`        | array of up to 4 `uint32` |    Optional | Bounded numeric context defined per diagnostic code |

### 17.1 `Subsystem`

Stable values include:

```text
SYSTEM
MEASUREMENT
FLOW
PRESSURE
TEMPERATURE
LEAK
STORAGE
POWER
BLE
NRF_LINK
CELLULAR
MQTT
HTTP
SECURITY
CONFIGURATION
```

Diagnostic records shall not contain secrets, raw AT transcripts with credentials, unrestricted stack dumps, or arbitrary memory contents.

## 18. `CommandRequest`

`CommandRequest` is an inbound application object and is not placed directly in the outbound record envelope.

| Field                | Type                 |    Required | Meaning                                               |
| -------------------- | -------------------- | ----------: | ----------------------------------------------------- |
| `command_id`         | `uint64`             |         Yes | Originator-scoped command identity for deduplication  |
| `command_type`       | `uint16` enum        |         Yes | Stable command discriminator                          |
| `schema_version`     | `uint16`             |         Yes | Command schema version                                |
| `issued_at_unix_ms`  | `uint64`             | Conditional | Origin time when supplied and trusted                 |
| `expires_at_unix_ms` | `uint64`             | Conditional | Command expiration when wall-clock policy supports it |
| `requested_by`       | `string<48>`         |    Optional | Authenticated principal identifier, not display text  |
| `parameters`         | bounded typed object |         Yes | Command-specific fields                               |

### 18.1 Command rules

1. Protocol authentication occurs before command authorization.
2. `command_id` deduplication shall prevent repeated state mutation.
3. Unsupported command types or versions shall be rejected explicitly.
4. Unknown parameters shall follow versioning policy; they shall not be silently interpreted.
5. Expired commands shall not execute.
6. Transport acknowledgement shall not represent command completion.
7. Commands shall not carry arbitrary executable text or unrestricted memory/register access.

## 19. `CommandResult`

| Field                  | Type                 |    Required | Meaning                                  |
| ---------------------- | -------------------- | ----------: | ---------------------------------------- |
| `command_id`           | `uint64`             |         Yes | Identity of the corresponding request    |
| `command_type`         | `uint16` enum        |         Yes | Corresponding command type               |
| `result_code`          | `CommandResultCode`  |         Yes | Final or accepted intermediate result    |
| `error_code`           | `uint32`             | Conditional | Stable product error when not successful |
| `completed_at_unix_ms` | `uint64`             | Conditional | Completion time when valid               |
| `applied_revision`     | `uint32`             |    Optional | Resulting config/calibration revision    |
| `details`              | bounded typed object |    Optional | Non-secret command-specific result       |

### 19.1 `CommandResultCode`

| Code | Name                         | Meaning                                                    |
| ---: | ---------------------------- | ---------------------------------------------------------- |
|    0 | `COMMAND_RESULT_ACCEPTED`    | Accepted for later bounded processing; not yet complete    |
|    1 | `COMMAND_RESULT_SUCCEEDED`   | Operation completed successfully                           |
|    2 | `COMMAND_RESULT_REJECTED`    | Request validly parsed but not permitted or not acceptable |
|    3 | `COMMAND_RESULT_INVALID`     | Schema or field validation failed                          |
|    4 | `COMMAND_RESULT_UNSUPPORTED` | Type or version unsupported                                |
|    5 | `COMMAND_RESULT_EXPIRED`     | Request expired before execution                           |
|    6 | `COMMAND_RESULT_CONFLICT`    | Current state/revision prevents application                |
|    7 | `COMMAND_RESULT_FAILED`      | Attempted operation failed                                 |

## 20. Configuration data contract

### 20.1 `ConfigurationChangeSet`

| Field           | Type          | Required | Meaning                                                    |
| --------------- | ------------- | -------: | ---------------------------------------------------------- |
| `base_revision` | `uint32`      |      Yes | Revision expected by requester                             |
| `change_count`  | `uint16`      |      Yes | Number of bounded changes                                  |
| `changes`       | bounded array |      Yes | Typed key/value changes permitted by configuration catalog |
| `apply_mode`    | `ApplyMode`   |      Yes | Requested application boundary                             |

### 20.2 `ApplyMode`

| Code | Name                 | Meaning                                     |
| ---: | -------------------- | ------------------------------------------- |
|    0 | `APPLY_WHEN_SAFE`    | Apply at next service-defined safe boundary |
|    1 | `APPLY_AFTER_REBOOT` | Persist now and activate after reboot       |
|    2 | `VALIDATE_ONLY`      | Validate without commit or apply            |

Immediate unsafe apply is not a generic mode. A configuration item may define a safe immediate boundary in its owning contract.

### 20.3 `ConfigurationResult`

| Field               | Type                |    Required | Meaning                        |
| ------------------- | ------------------- | ----------: | ------------------------------ |
| `request_id`        | `uint64`            |         Yes | Corresponding request identity |
| `result_code`       | `CommandResultCode` |         Yes | Overall result                 |
| `previous_revision` | `uint32`            |         Yes | Revision before operation      |
| `new_revision`      | `uint32`            | Conditional | Committed revision on success  |
| `apply_state`       | `ApplyState`        |         Yes | Runtime activation status      |
| `field_errors`      | bounded array       |    Optional | Per-field validation failures  |

### 20.4 Configuration invariants

1. Fields shall come from an allowlisted configuration catalog.
2. Unknown keys shall not be persisted.
3. Cross-field validation occurs before commit.
4. Required A/B persistent commit completes before success is reported.
5. Runtime apply occurs only at the owning service's safe boundary.
6. Credential values shall use a protected provisioning contract and shall not be echoed in results.

## 21. Delivery result model

Protocol adapters return normalized results to `RemoteDeliveryService`.

### 21.1 `DeliveryAttemptResult`

| Field               | Type                 | Required | Meaning                                        |
| ------------------- | -------------------- | -------: | ---------------------------------------------- |
| `record_id`         | `uint64`             |      Yes | Attempted record                               |
| `channel`           | `RemoteChannel`      |      Yes | MQTT or HTTP                                   |
| `attempt_id`        | `uint32`             |      Yes | Volatile/local attempt identity                |
| `result_code`       | `DeliveryResultCode` |      Yes | Normalized outcome                             |
| `protocol_code`     | `int32`              | Optional | MQTT/HTTP/modem-specific bounded code          |
| `retry_after_ms`    | `uint32`             | Optional | Server/protocol suggested minimum delay        |
| `remote_receipt_id` | `string<64>`         | Optional | Non-secret server receipt/correlation identity |

### 21.2 `DeliveryResultCode`

| Code | Name                          | Meaning                                                                  |
| ---: | ----------------------------- | ------------------------------------------------------------------------ |
|    0 | `DELIVERY_ACCEPTED`           | Remote application acceptance is sufficient for this contract            |
|    1 | `DELIVERY_TRANSPORT_ACK_ONLY` | Transport acknowledged; further application confirmation may be required |
|    2 | `DELIVERY_RETRYABLE`          | Temporary failure; policy may retry                                      |
|    3 | `DELIVERY_PERMANENT_FAILURE`  | Retry with unchanged record is not useful/permitted                      |
|    4 | `DELIVERY_AUTH_FAILURE`       | Authentication/authorization failure requiring controlled recovery       |
|    5 | `DELIVERY_UNSUPPORTED`        | Schema/record/operation unsupported                                      |
|    6 | `DELIVERY_EXPIRED`            | Record no longer eligible for delivery                                   |
|    7 | `DELIVERY_PARTIAL`            | Batch or multi-part operation only partially accepted                    |
|    8 | `DELIVERY_CANCELLED`          | Attempt cancelled before accepted completion                             |

### 21.3 Delivery rules

1. Adapters shall not delete records directly.
2. `RemoteDeliveryService` interprets normalized results with delivery policy.
3. HTTP batch results shall produce one effective result per record.
4. MQTT QoS success maps to `DELIVERY_ACCEPTED` only when the MQTT application contract declares it sufficient.
5. Lost response after possible remote acceptance is retryable only with the same `record_id` and documented deduplication.

## 22. Optional and missing fields

The semantic states are distinct:

| State                                    | Representation                                                      |
| ---------------------------------------- | ------------------------------------------------------------------- |
| Field not applicable to this record type | Omit field                                                          |
| Field supported but unavailable          | Omit numeric value and set associated unavailable quality/state     |
| Field available with value zero          | Include zero                                                        |
| Field invalid                            | Include value only if diagnostically useful and set invalid quality |
| Field unknown to older consumer          | Handle according to version policy                                  |

`null`, empty string, zero, maximum integer, and omitted field shall not be treated as interchangeable.

The encoding document shall state whether unavailable optional objects are omitted or encoded as `null`. Producers shall use one canonical representation per schema version.

## 23. Validation order

Consumers shall validate input in this order where applicable:

1. transport/frame integrity and bounded size;
2. authentication and channel authorization;
3. syntax/decoding;
4. common envelope required fields;
5. supported schema version;
6. `record_type` or command discriminator;
7. primitive type and length constraints;
8. numeric and enum ranges;
9. cross-field invariants;
10. duplicate/replay status;
11. product authorization and current-state guards;
12. persistent and runtime application policy.

Failure at one stage shall not partially apply later-stage effects.

## 24. Size and boundedness rules

1. Every string, byte sequence, array, object collection, and context field shall have an explicit maximum.
2. Decoders shall reject or safely skip oversized content before allocation/copy.
3. HTTP batching shall respect both record count and encoded byte limit.
4. MQTT payload size shall remain within the configured modem, broker, and memory limit.
5. Diagnostic context is fixed/bounded; arbitrary logs are not part of this contract.
6. BLE and internal-link fragmentation shall reconstruct into a bounded application object.
7. A valid but unsupported extension shall not cause buffer overrun or unbounded recursion.

Exact encoded-size budgets are assigned after the encoding and modem constraints are confirmed.

## 25. Ordering and duplicate semantics

1. `record_id` provides identity, not guaranteed chronological ordering.
2. `created_at_unix_ms` shall not be the sole ordering key when time quality is weak.
3. `snapshot_sequence` orders snapshots from the relevant device sequence domain.
4. Remote services shall tolerate retry duplicates with the same `(device_id, record_id)`.
5. Receiving the same `record_id` with a different payload is a contract violation and shall be diagnosed.
6. MQTT and HTTP delivery of the same record shall converge on one logical remote record.
7. Commands use `command_id` for duplicate execution protection independently of outbound `record_id`.

## 26. Privacy and secret handling

The following shall not appear in common records:

* passwords, access tokens, private keys, shared secrets, or complete certificates containing sensitive provisioning data;
* unrestricted SIM identifiers unless explicitly required and approved;
* raw memory contents;
* unfiltered AT-command transcripts;
* personally identifying customer data not required by the product contract;
* BLE pairing secrets;
* internal storage CRC keys or protected calibration secrets.

Diagnostic fields shall use normalized codes and bounded non-secret context.

## 27. Protocol mapping requirements

### 27.1 MQTT mapping

The MQTT contract shall define:

* topic by record type;
* envelope representation;
* integer precision behavior;
* QoS and retained flag;
* application acknowledgement when required;
* maximum payload and duplicate mapping.

### 27.2 HTTP mapping

The HTTP contract shall define:

* endpoint and method by record type;
* content type and envelope representation;
* single-record or batch body;
* idempotency mapping from `(device_id, record_id)`;
* per-record response and partial acceptance;
* status-code mapping to `DeliveryResultCode`.

### 27.3 BLE mapping

The BLE contract shall define:

* which common fields are exposed locally;
* GATT characteristic or command mapping;
* MTU and fragmentation;
* authorization and read/write permissions;
* local transaction/request identity.

### 27.4 Internal-link mapping

Internal links shall define:

* compact type identifiers;
* byte order and integer width;
* frame length and CRC/integrity;
* fragmentation and reassembly;
* sequence/ACK semantics distinct from application identity.

## 28. Illustrative semantic examples

Examples are informative until copied into protocol-specific canonical test vectors.

### 28.1 Telemetry example

```json
{
  "schema_version": 1,
  "record_type": "telemetry",
  "record_id": "4294967297",
  "device_id": "WM-000001",
  "created_at_unix_ms": "1784102400000",
  "uptime_ms": "86400000",
  "time_quality": "synchronized",
  "priority": "normal",
  "expires_after_ms": 86400000,
  "payload": {
    "snapshot_sequence": "1024",
    "snapshot_quality_flags": 1,
    "temperature": {
      "temperature_mdeg_c": 27625,
      "quality_flags": 1,
      "age_ms": 120
    },
    "flow_rate": {
      "flow_rate_ml_per_h": 125400,
      "quality_flags": 1,
      "age_ms": 120
    },
    "pressure": {
      "pressure_pa": 315200,
      "quality_flags": 1,
      "age_ms": 95
    },
    "total_forward_volume_ml": "1042800",
    "leak_state": "clear",
    "leak_flags": 0,
    "device_health": "ok",
    "config_revision": 7,
    "calibration_revision": 3
  }
}
```

The example encodes `uint64` fields as decimal strings to avoid loss in JSON consumers with limited integer precision. The final JSON mapping shall be confirmed by protocol documents.

### 28.2 Unavailable pressure example

```json
{
  "pressure": {
    "quality_flags": 34,
    "age_ms": 4294967295
  }
}
```

Here the numeric value is omitted and flags indicate unavailable plus timeout. The exact JSON field layout remains informative.

## 29. C model guidance

The firmware may use structures equivalent to the following concepts:

```c
typedef struct {
    bool has_value;
    int32_t value;
    uint32_t quality_flags;
    uint32_t age_ms;
    uint64_t sampled_at_unix_ms;
} MeasurementI32;

typedef struct {
    uint16_t schema_version;
    uint16_t record_type;
    uint64_t record_id;
    uint64_t created_at_unix_ms;
    uint64_t uptime_ms;
    uint32_t expires_after_ms;
    uint8_t time_quality;
    uint8_t priority;
} CommonRecordHeader;
```

These examples are not final ABI. Public repository contracts shall avoid flexible arrays, raw pointers in persistent/wire data, compiler bit-fields, and implicit padding-dependent serialization.

## 30. Deterministic test requirements

Tests shall cover at least:

* minimum, maximum, zero, negative, and overflow-boundary values;
* absent, invalid, unavailable, stale, and valid measurements;
* reserved and unknown enum/flag values;
* stable `record_id` across MQTT retry and HTTP fallback;
* same ID with changed payload rejection;
* unsynchronized time;
* `uint64` JSON precision handling;
* optional fields and unknown forward-compatible fields;
* malformed lengths and oversized arrays/strings;
* duplicate commands and duplicate records;
* HTTP partial batch result mapped per record;
* command validation without partial side effects;
* configuration revision conflict;
* reboot recovery without changing recovered record identity;
* encoding/decoding round trip with byte-identical canonical output where required.

## 31. Architectural invariants

1. Semantic units are explicit and transport independent.
2. Zero is never used as a generic missing-measurement sentinel.
3. `record_id = 0` is invalid.
4. `(device_id, record_id)` identifies one immutable record payload.
5. MQTT and HTTP mappings preserve common record meaning.
6. Quality flags accompany measurement validity across every mapping.
7. Transport identifiers do not replace application identifiers.
8. A command result references the original `command_id`.
9. Configuration is not applied before validation and required commit.
10. Unknown or malformed input does not partially mutate domain state.
11. Secret material is absent from common records and traces.
12. Every variable-length field is bounded.

## 32. Open decisions

| Decision                    | Options/question                                                       | Affected documents                        |
| --------------------------- | ---------------------------------------------------------------------- | ----------------------------------------- |
| Final payload encoding      | JSON, CBOR, or per-channel encoding with one semantic schema           | MQTT, HTTP, BLE, test vectors             |
| `uint64` JSON mapping       | Decimal string or verified integer number                              | MQTT and HTTP message catalogs            |
| Record-ID generation        | Persistent counter, boot epoch plus sequence, or another proven scheme | Storage and remote delivery policy        |
| Encoded payload budget      | Maximum record and batch bytes                                         | MQTT, HTTP batching, modem integration    |
| Queue persistence           | Volatile, critical-only, or bounded full queue                         | Storage and delivery policy               |
| Reverse/net volume exposure | Required, optional, or product-variant dependent                       | Telemetry schema and product requirements |
| Event-code catalog location | Firmware event catalog or dedicated communication diagnostic catalog   | Event and diagnostic documents            |
| Remote command set          | Exact commands and eligible channels                                   | Security and message catalogs             |
| HTTP batch response         | Per-record status and receipt format                                   | HTTP API and delivery/batching            |
| MQTT application receipt    | Required record classes and receipt schema                             | MQTT topic/message catalog                |
| Exact string limits         | Confirmed by cloud, BLE, and modem constraints                         | All protocol mappings                     |

## 33. Definition of ready
s
This document may move from `Draft` to `Proposed` when:

* field names, logical types, units, and scaling are reviewed against firmware domain models;
* `record_id` generation and reboot uniqueness strategy are accepted;
* timestamp and time-quality semantics are accepted;
* telemetry optional fields match the product baseline;
* quality-flag ownership is reconciled with measurement documents;
* event, diagnostic, command, and configuration code catalogs have normative owners;
* payload encoding and `uint64` representation decisions are recorded;
* maximum encoded sizes fit STM32, EC200U-CN, BLE, and server constraints;
* MQTT and HTTP mappings can represent the same record without semantic loss;
* and deterministic tests cover the invariants and boundary cases.
