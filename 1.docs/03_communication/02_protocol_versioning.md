# Protocol Versioning and Compatibility

| Metadata | Value |
|---|---|
| Document ID | `COMM-VERSION-002` |
| Status | `Proposed` |
| Baseline | Independent version domains with explicit compatibility rules |
| Applies to | Common data schema, MQTT, HTTP, BLE, STM32–nRF52810 transport, STM32–EC200U integration, firmware, mobile application, and remote services |

## 1. Purpose

This document defines how communication contracts evolve without silently breaking deployed devices, coprocessor firmware, mobile applications, or remote services.

It specifies:

- independent version domains and their ownership;
- schema-version allocation and lifecycle;
- backward- and forward-compatibility rules;
- handling of unknown fields, enum values, flags, record types, and commands;
- breaking and non-breaking changes;
- negotiation and capability discovery for MQTT, HTTP, BLE, and internal links;
- deprecation, migration, rollback, and mixed-version deployment;
- version metadata required in diagnostics and status records;
- and validation gates for releasing a new contract version.

This document does not define the content of individual MQTT topics, HTTP endpoints, BLE characteristics, or internal frames. Those documents apply the rules defined here.

## 2. Versioning objectives

The versioning model shall:

1. allow old and new components to coexist during staged deployment;
2. reject incompatible input before domain mutation;
3. permit safe optional extensions where explicitly allowed;
4. distinguish application-schema changes from transport changes;
5. support deterministic Linux compatibility tests;
6. prevent firmware version strings from being used as protocol versions;
7. preserve record identity across protocol adaptation and fallback;
8. make deprecation and removal explicit;
9. avoid silent reinterpretation of existing numeric codes;
10. permit rollback only where stored and wire-visible data remain compatible.

## 3. Normative terminology

| Term | Meaning |
|---|---|
| Version domain | Independently versioned contract with one normative owner |
| Producer | Component that creates or sends an object |
| Consumer | Component that validates and processes an object |
| Backward compatible | New consumer can process data produced by an older supported producer |
| Forward compatible | Older consumer can safely process or safely reject data produced by a newer producer |
| Breaking change | Change that may alter interpretation, validation, required behavior, or supported exchange |
| Non-breaking change | Change that remains safe under the documented compatibility rules |
| Unknown | Identifier or field not defined by the consumer's implemented contract |
| Unsupported | Known category/version that the consumer intentionally does not implement |
| Deprecated | Still supported but scheduled for future removal |
| Removed | No longer accepted or produced in the specified version |
| Negotiation | Exchange used to select a mutually supported version/capability set |
| Capability | Independently discoverable optional feature within a supported contract |

## 4. Independent version domains

Versions shall not be combined into one global product version.

| Version domain | Example owner | Purpose |
|---|---|---|
| Common semantic schema | `01_common_data_contract.md` | Meaning of records and common fields |
| MQTT contract | MQTT documents | Topics, payload mapping, subscriptions, acknowledgements |
| HTTP API | HTTP documents/remote service | Endpoint paths, methods, media types, responses |
| BLE GATT contract | BLE documents/nRF52810 firmware | Services, characteristics, permissions, payload mapping |
| STM32–nRF transport | Internal-link document | Frame header, message types, CRC, sequencing, handshake |
| STM32–EC200U integration | Modem-integration document | Supported modem operations and normalized behavior |
| Persistent record layout | Firmware storage documents | F-RAM record fields, CRC, A/B slot, migration |
| Configuration schema | Configuration repository contract | Configuration keys, types, validation, revisions |
| Calibration schema | Calibration/profile contract | Sensor-profile and calibration fields |
| Firmware release | Build/release process | Executable software release identity |
| Hardware revision | Hardware documentation | Physical product revision |

An increment in one domain does not automatically increment another. A change shall update every affected domain and traceability entry, but unrelated versions remain unchanged.

## 5. Version identifiers

### 5.1 Common schema version

`schema_version` is a required `uint16` integer in every `CommonRecordEnvelope` and inbound command/configuration object.

Rules:

1. `schema_version = 0` is invalid and reserved.
2. Version numbers increase monotonically within the common schema domain.
3. A number is never reused after release.
4. Version meaning is defined in the version registry, not inferred from firmware version.
5. Each version is a complete logical contract, not a patch applied implicitly to an older contract.
6. Producers shall emit exactly one declared schema version per object.
7. Consumers shall validate the version before interpreting version-dependent fields.

### 5.2 Protocol-contract versions

Each protocol domain shall expose an explicit version through its own mechanism:

| Domain | Required representation |
|---|---|
| MQTT | Contract/version field in payload or versioned topic namespace as accepted by MQTT contract |
| HTTP | Versioned API path, media type, header, or bounded combination defined by HTTP API contract |
| BLE | Readable protocol-version characteristic and/or version in command envelope |
| STM32–nRF | Version in handshake and frame/message contract |
| STM32–EC200U | Normalized integration capability/version in firmware configuration and diagnostics |

Protocol version and common `schema_version` are distinct. For example, MQTT contract version 2 may carry common schema version 4.

### 5.3 Firmware and component versions

Firmware versions identify implementations, not wire compatibility.

Status/identity reporting should expose:

- STM32 firmware version;
- firmware variant;
- nRF52810 firmware version;
- modem firmware version when available;
- common schema versions supported/produced;
- protocol-contract versions supported;
- sensor-profile and calibration versions.

## 6. Version registry

Every version domain shall maintain a normative registry. The registry may be a section in the owning document or a dedicated machine-readable file.

Minimum fields:

| Field | Meaning |
|---|---|
| Domain | Version domain name |
| Version | Stable numeric or structured identifier |
| Status | Draft, Proposed, Accepted, Deprecated, or Removed |
| Introduced by | Decision, change request, or release reference |
| Producer support | Components that may produce it |
| Consumer support | Components that may consume it |
| Compatibility range | Explicitly supported earlier/later versions |
| Breaking changes | Summary and migration reference |
| Effective date/release | First intended deployment |

Example:

| Domain | Version | Status | Compatibility note |
|---|---:|---|---|
| Common schema | 1 | Proposed | Initial transport-neutral record contract |

This example does not make schema version 1 `Accepted`; acceptance follows the document lifecycle.

## 7. Compatibility model

### 7.1 Consumer behavior

For every received object, a consumer shall choose exactly one outcome:

1. accept and fully process;
2. accept supported fields and safely ignore permitted unknown optional fields;
3. reject as unsupported version/type/capability;
4. reject as invalid or malformed;
5. quarantine/store without processing only if a separate bounded policy explicitly permits it.

Silent reinterpretation is prohibited.

### 7.2 Supported-version set

Each consumer shall define:

- versions it can decode;
- versions it can validate;
- versions it can execute/apply for commands and configuration;
- versions it can produce;
- preferred version when several are mutually supported.

Support may be a discrete set rather than a continuous minimum/maximum range. Implementations shall not assume all versions between `min` and `max` are supported unless the contract guarantees continuity.

### 7.3 Read versus write compatibility

A component may accept more versions than it produces. For example:

- firmware may consume configuration schema 2 and 3 but emit only schema 3;
- server may ingest telemetry schema 1 and 2 while returning command schema 2 only;
- mobile application may read BLE contract 1 and 2 but write only contract 2.

Producer and consumer capability shall be reported separately where negotiation requires it.

## 8. Change classification

### 8.1 Breaking changes

The following are breaking unless a domain-specific rule proves compatibility:

- removing or renaming a field;
- changing a field's meaning, unit, scale, signedness, width, range, default, or required status;
- reusing an enum/code/flag value for a new meaning;
- changing `record_id`, timestamp, deduplication, acknowledgement, or idempotency semantics;
- changing an optional field to required for existing objects;
- changing validation so previously valid data becomes invalid in a way old producers cannot detect;
- changing a command from read-only to state-changing;
- changing authorization or persistent side effects;
- changing topic/endpoint/UUID/frame meaning without version separation;
- changing byte order, frame layout, CRC coverage, or fragmentation semantics;
- changing HTTP status interpretation or MQTT delivery-success semantics;
- changing a default that affects wire-visible or persistent behavior;
- changing unknown-field behavior from ignore to execute/apply.

Breaking changes require a new version, migration plan, compatibility tests, and traceability update.

### 8.2 Potentially non-breaking changes

The following may be non-breaking only when all consumers follow the documented extension rules:

- adding an optional field with no side effects and a safe absence meaning;
- adding a new record/event subtype that old consumers can explicitly reject;
- adding a new enum value when consumers preserve/report unknown values or reject safely;
- adding a new flag in a reserved extension range when unknown flags do not alter existing meaning;
- increasing a maximum within already allocated and tested decoder capacity;
- adding a new HTTP endpoint without modifying existing endpoints;
- adding a new MQTT topic without changing existing topic semantics;
- adding a new optional BLE characteristic without changing existing handles/meaning;
- adding a capability that is inactive unless negotiated.

Every such change still requires a version-registry entry and tests. “Optional” alone does not guarantee compatibility.

### 8.3 Documentation-only changes

Spelling, formatting, examples, diagrams, or clarifications may keep the same protocol/schema version only when they do not alter normative meaning.

If a clarification changes how two reasonable implementations behave, it is a contract change and shall be classified accordingly.

## 9. Field evolution rules

### 9.1 Adding fields

A new field shall define:

- first schema version containing it;
- required or optional status;
- type, range, unit, and maximum encoded size;
- behavior when absent;
- behavior for consumers that do not recognize it;
- privacy/security classification;
- deterministic test vectors.

New required fields require a new incompatible schema version unless negotiation ensures old producers cannot send to the new-only consumer path.

### 9.2 Removing fields

Removal follows:

1. mark field deprecated;
2. stop relying on it in new consumers;
3. maintain production during the overlap window;
4. confirm usage/compatibility evidence;
5. remove only in a new breaking version;
6. retain the numeric/tag identifier as reserved where binary mappings exist.

### 9.3 Renaming fields

Renaming a wire-visible field is removal plus addition. Text aliases may be accepted temporarily only when duplicate/conflict behavior is explicit.

### 9.4 Changing units or scale

A field's unit or scale shall never change in place. Add a new field or introduce a new schema version with explicit migration.

## 10. Unknown fields

### 10.1 Telemetry, status, event, and diagnostic records

For non-state-changing inbound processing:

- consumers may ignore unknown optional fields when the schema version and envelope are otherwise supported;
- consumers shall not fail merely because field order differs in order-independent encodings;
- consumers shall enforce total size, nesting, and count limits before ignoring fields;
- unknown fields shall not overwrite known fields through aliasing or duplicate keys;
- forwarding components should preserve unknown fields only when the contract explicitly requires transparent forwarding.

### 10.2 Commands and configuration

For state-changing requests:

- unknown command parameters or configuration keys shall be rejected by default;
- an explicit extension container may permit namespaced optional fields;
- ignored unknown fields shall never affect authorization, persistence, calibration, measurement, or runtime apply;
- partial application after unknown-field rejection is prohibited.

### 10.3 Duplicate fields

Text decoders shall reject duplicate occurrences of a field unless the encoding contract defines a canonical merge rule. “First wins” and “last wins” are prohibited defaults because different parsers may disagree.

## 11. Unknown enum values and flags

### 11.1 Enum values

Consumers shall not map an unknown numeric enum to an existing known value.

Allowed outcomes:

- preserve numeric value and report `UNKNOWN` at presentation level;
- reject the object if the enum controls execution, authorization, persistence, or safety;
- accept the object with degraded interpretation when the field is informational and the owning contract permits it.

### 11.2 Flags

- Producers shall set reserved bits to zero.
- Consumers shall mask known bits only for decision logic.
- Unknown bits shall be retained for diagnostics when practical.
- A state-changing request with unknown behavior-affecting flags shall be rejected.
- Adding a flag that changes interpretation of existing fields requires explicit version/capability handling.

## 12. Unknown record and message types

| Input category | Required default behavior |
|---|---|
| Unknown outbound record observed by server | Reject or store as opaque only under explicit policy; never reinterpret |
| Unknown inbound command | Return `UNSUPPORTED`; no side effects |
| Unknown BLE command | Return unsupported command result |
| Unknown STM32–nRF message | NACK/drop according to negotiated transport contract |
| Unknown MQTT topic | Ignore only when not subscribed/required; diagnose unexpected subscribed traffic |
| Unknown HTTP endpoint | Standard versioned not-found/unsupported response |

Unknown critical message types shall not be silently ignored when doing so would leave a transaction ambiguous. The protocol contract shall define a negative response or timeout outcome.

## 13. Common schema evolution

### 13.1 Production rule

A producer emits a schema version supported by the selected remote/local consumer contract.

The same logical record shall not be reinterpreted differently merely because it is sent through MQTT or HTTP. If channel adapters require different wire mappings, each mapping shall identify the common schema version it represents.

### 13.2 Cross-channel fallback

When a record falls back from MQTT to HTTP or HTTP to MQTT:

- `device_id`, `record_id`, record type, semantic payload, timestamps, and quality remain unchanged;
- protocol-specific contract versions and envelopes may differ;
- the target channel shall support the record's common schema version;
- otherwise fallback is ineligible and produces an explicit normalized result;
- conversion to an older schema creates a distinct derived record only if a documented transformation contract permits it.

Adapters shall not silently drop fields to make an unsupported fallback appear successful.

### 13.3 Transformation

Schema transformation is permitted only when:

- source and target versions are known;
- transformation is deterministic;
- no required semantic information is lost, or the documented loss is acceptable for that record class;
- quality and identity rules remain valid;
- test vectors cover boundary and unknown-field behavior.

## 14. MQTT versioning

The MQTT contract shall define:

- supported MQTT protocol version independently of application-contract version;
- application contract/schema version location;
- topic namespace versioning strategy;
- payload media/encoding version;
- command and result compatibility;
- subscription behavior during mixed-version deployment;
- application acknowledgement version.

### 14.1 Topic-version rules

If the topic contains a contract version:

- old and new topic versions may coexist during migration;
- a device subscribes only to versions it supports;
- servers shall not publish a newer command version to an older subscription;
- retained messages shall be evaluated against current version and expiration before application;
- duplicate delivery across old/new topics shall preserve application identity.

If topic paths are unversioned, every payload shall carry sufficient version metadata and consumers shall reject unsupported versions explicitly.

### 14.2 MQTT session compatibility

Session restoration shall not assume that subscriptions, retained messages, or queued commands remain compatible after firmware/contract upgrade. The MQTT adapter shall reconcile required subscriptions against the active supported contract set.

## 15. HTTP versioning

The HTTP contract shall use one canonical API-version mechanism. Acceptable mechanisms include:

- versioned path such as `/v1/...`;
- versioned media type;
- explicit version header.

Using several independent mechanisms for the same API version should be avoided. If more than one is required, conflict precedence shall be defined.

### 15.1 Request rules

- API version shall be resolved before body application.
- Body `schema_version` remains required for common records.
- Idempotency keys remain stable across retry of the same record/batch contract.
- Redirect behavior across API versions shall be explicit; state-changing requests shall not follow arbitrary redirects.
- Unsupported API versions return a stable unsupported-version response.

### 15.2 Batch rules

An HTTP batch shall declare one supported batch-envelope version. Individual records retain their own common `schema_version` if mixed versions are permitted.

The HTTP contract shall define whether:

- mixed-schema batches are allowed;
- partial acceptance is reported per record;
- unsupported records reject the whole batch or only affected records;
- retry subsets preserve original `record_id` and idempotency.

## 16. BLE versioning

The BLE GATT contract shall expose enough information for the mobile application to determine compatibility before issuing protected writes.

Minimum capability includes:

- BLE/GATT contract version;
- supported command-schema versions;
- supported optional capabilities;
- device/firmware identity required for service diagnostics.

### 16.1 GATT evolution

- Existing characteristic meaning, unit, permissions, and notification semantics shall not change in place.
- New optional characteristics may be added when old clients can safely ignore them.
- Changed meaning requires a new characteristic UUID or new negotiated service contract version.
- Handle numbers are not stable application identifiers.
- A client shall not infer capability solely from firmware version.

### 16.2 Command evolution

BLE command envelopes shall contain command type and schema version. Unknown state-changing parameters are rejected by default.

## 17. STM32–nRF52810 transport versioning

The internal link shall negotiate compatibility before normal command/event exchange.

### 17.1 Handshake information

The startup handshake should include:

- transport contract versions supported;
- preferred transport version;
- maximum frame payload;
- supported message/command schema versions;
- capability bitmap;
- nRF52810 firmware version;
- reset/session identity.

### 17.2 Selection rule

Both peers select the highest mutually supported accepted transport version unless a compatibility policy selects another version.

If no common version exists:

- normal BLE-dependent application exchange remains disabled;
- both peers expose a diagnosable incompatible-version state;
- STM32 measurement operation continues;
- neither peer guesses a frame layout.

### 17.3 Frame parsing

A frame-version mismatch is rejected before message payload interpretation. Unknown frame flags affecting integrity, length, sequencing, or fragmentation are fatal to that frame.

## 18. STM32–EC200U integration versioning

The modem integration contract normalizes vendor behavior. It does not assume all EC200U firmware revisions expose identical command behavior.

At initialization, the modem service should identify:

- modem model and firmware revision;
- supported MQTT/HTTP/TLS operations required by the product;
- known quirks/workarounds profile;
- configured integration profile version.

Unsupported or unqualified modem firmware shall produce an explicit degraded/incompatible status rather than silently selecting untested command sequences.

## 19. Capability discovery

Versions describe contract revisions; capabilities describe optional features.

Example capabilities:

```text
MQTT_TELEMETRY
MQTT_COMMANDS
MQTT_APPLICATION_RECEIPTS
HTTP_SINGLE_UPLOAD
HTTP_BATCH_UPLOAD
HTTP_PARTIAL_ACCEPTANCE
BLE_CONFIGURATION
BLE_DIAGNOSTICS
REMOTE_CHANNEL_FALLBACK
REVERSE_VOLUME
```

Rules:

1. Capability identifiers are stable and never reused.
2. Absence means unsupported, not disabled, unless configuration state is reported separately.
3. A capability may require a minimum contract/schema version.
4. Capability negotiation shall not bypass authorization or configuration validation.
5. Unknown capabilities may be ignored for informational discovery but not assumed enabled.

## 20. Negotiation strategy by channel

| Channel | Negotiation/discovery approach |
|---|---|
| MQTT | Device status/capability record plus server-side routing to supported topics/payloads |
| HTTP | Versioned endpoint and optional capability/registration response |
| BLE | Readable version/capability characteristic before protected commands |
| STM32–nRF | Mandatory startup handshake before normal messages |
| STM32–EC200U | Local capability/profile detection during modem initialization |

Negotiation results are volatile unless the owning contract explicitly permits cached persistence. Cached capabilities shall be invalidated when peer identity/version changes.

## 21. Commands and state-changing requests

State-changing inputs use stricter compatibility rules than telemetry.

1. Exact command type and supported schema version are required.
2. Unknown required or behavior-affecting fields reject the entire request.
3. Command deduplication occurs before execution.
4. Version rejection has no persistent or runtime side effects.
5. A result reports `UNSUPPORTED` separately from `INVALID` and `UNAUTHORIZED`.
6. Downgrade to an older command schema is not automatic.
7. Retry uses the same command identity and schema unless the originator creates a new request.

## 22. Configuration and calibration versioning

Communication schema, configuration schema, calibration schema, and persistent layout are separate domains.

An inbound configuration request may contain:

- command/envelope schema version;
- configuration schema version;
- expected `base_revision` of the current configuration instance;
- typed changes.

Rules:

- schema compatibility is checked before revision conflict;
- unsupported configuration fields are not persisted;
- migrations run through repository/storage contracts, not communication adapters;
- calibration/profile transformations require measurement-domain validation;
- a new communication schema does not authorize writing a newer configuration schema automatically.

## 23. Persistent data and rollback

Firmware rollback is safe only if the older firmware can:

- read the active persistent schema;
- safely ignore or reject newer records without corrupting them;
- preserve required record identities and queue state;
- handle current BLE/internal-link peer versions;
- and communicate with available remote API/topic versions.

When these conditions do not hold, rollback requires an explicit migration or restore procedure.

Persistent record layouts shall contain their own magic/type/version/length/integrity metadata. Communication `schema_version` shall not be used as a substitute for storage-layout version.

## 24. Deprecation lifecycle

The normal lifecycle is:

```text
Draft -> Proposed -> Accepted -> Deprecated -> Removed
```

### 24.1 Deprecation requirements

Before deprecation:

- replacement version is accepted;
- producers and consumers supporting the old version are identified;
- migration and rollback behavior are documented;
- telemetry/diagnostics can show remaining old-version use.

Before removal:

- minimum supported firmware/mobile/server versions are updated;
- compatibility tests confirm rejection behavior;
- retained MQTT messages and queued records are handled;
- persistent data migration is complete or explicitly unsupported;
- deployment evidence shows the old version is no longer required.

No fixed time duration is assumed until product deployment policy defines one.

## 25. Mixed-version deployment

During staged deployment, combinations may include:

- new server with old device firmware;
- old server with new device firmware;
- new STM32 firmware with old nRF52810 firmware;
- old mobile application with new BLE service;
- records queued before firmware update and sent afterward;
- MQTT primary delivery followed by HTTP fallback using a different protocol version.

Each release shall define a compatibility matrix covering supported combinations.

Example matrix template:

| Producer | Consumer | Schema/protocol | Expected result |
|---|---|---|---|
| STM32 release A | Server release B | Common schema 1 via MQTT contract 1 | Accept |
| STM32 release B | Server release A | Common schema 2 via HTTP API v1 | Accept optional fields or reject explicitly |
| nRF contract 2 | STM32 contract 1 | No common internal-link version | BLE feature unavailable; measurement continues |

## 26. Error model

Version-related errors shall be distinguishable:

| Error category | Meaning |
|---|---|
| `UNSUPPORTED_SCHEMA_VERSION` | Common semantic schema unsupported |
| `UNSUPPORTED_PROTOCOL_VERSION` | MQTT/HTTP/BLE/internal contract unsupported |
| `UNSUPPORTED_API_VERSION` | HTTP API version unsupported |
| `UNSUPPORTED_MESSAGE_TYPE` | Version understood but type unsupported |
| `UNSUPPORTED_COMMAND` | Command not implemented/allowed in supported version |
| `UNSUPPORTED_CAPABILITY` | Requested optional capability unavailable |
| `INVALID_VERSION_FIELD` | Missing, zero, malformed, or out-of-range version |
| `VERSION_NEGOTIATION_FAILED` | No compatible version found |
| `VERSION_CONFLICT` | Multiple version indicators disagree |
| `MIGRATION_REQUIRED` | Persistent/config data requires controlled migration |

Errors shall not echo secrets or expose unnecessary server/device internals.

## 27. Security considerations

1. Version negotiation shall not permit downgrade to a known insecure contract without explicit policy.
2. Authentication occurs before accepting state-changing negotiation or capability claims where the channel permits it.
3. Version fields are untrusted input and are validated before allocation or parsing decisions.
4. Unknown fields remain subject to size and nesting limits even when ignored.
5. Deprecated insecure cipher/authentication behavior is controlled by security policy, not retained solely for compatibility.
6. Server redirects, MQTT retained commands, and cached BLE capability data shall not bypass active-version validation.
7. Error responses reveal only the bounded supported-version information allowed by the security contract.

## 28. Implementation guidance

### 28.1 Version dispatch

Use explicit dispatch rather than scattered conditionals:

```c
switch (schema_version) {
case 1:
    return DecodeSchemaV1(input, output);
default:
    return COMM_ERR_UNSUPPORTED_SCHEMA_VERSION;
}
```

For several supported versions, version-specific decoders should map into one current internal domain model after validation.

### 28.2 Preserve old decoders intentionally

Old decoders shall remain only while their versions are supported. Tests, size budgets, and security review apply to every retained decoder.

### 28.3 Do not branch domain logic by transport

MQTT and HTTP decoding should converge into the same validated common objects. Domain behavior shall not depend on whether an equivalent command arrived through MQTT, HTTP, or BLE except where authorization policy explicitly differs.

## 29. Version metadata reporting

`DeviceStatusRecord` or an approved capability record should report:

- produced common schema version;
- supported common schema versions;
- MQTT contract version;
- HTTP API version;
- BLE contract version;
- STM32–nRF transport version;
- configuration and calibration schema versions;
- component firmware versions;
- active/deprecated version flags where useful.

Reporting shall be bounded. A compact bitmap/range/list representation may be used by constrained channels.

## 30. Deterministic validation requirements

Tests shall cover:

- accepted exact-version exchange;
- older producer to newer consumer;
- newer producer to older consumer;
- unknown optional telemetry field;
- unknown state-changing command field;
- duplicate field rejection;
- unknown enum and flag behavior;
- unsupported record/message/command type;
- zero, missing, malformed, and maximum version values;
- no common STM32–nRF transport version;
- HTTP unsupported API version;
- MQTT old/new topic coexistence;
- fallback where HTTP cannot carry the queued schema version;
- valid deterministic schema transformation;
- rejection of lossy/undefined transformation;
- queued old-version record after firmware update;
- persistent schema newer than rollback firmware;
- downgrade attack attempt;
- deprecation warning and removed-version rejection;
- byte-identical normalized traces across repeated Linux runs.

## 31. Release checklist

Before accepting a new version:

- [ ] Normative owner and registry entry exist.
- [ ] Change is classified as breaking or non-breaking.
- [ ] All affected protocol and storage domains are identified.
- [ ] Producer and consumer compatibility matrix is updated.
- [ ] Unknown-field/enum/flag behavior is documented.
- [ ] Migration, fallback, and rollback behavior are documented.
- [ ] Maximum encoded size and memory impact are measured.
- [ ] Security and downgrade impact are reviewed.
- [ ] Linux positive and negative test vectors pass.
- [ ] MQTT, HTTP, BLE, and internal-link mappings are updated where affected.
- [ ] Mobile, cloud, nRF52810, and STM32 release dependencies are recorded.
- [ ] Deprecated versions have monitoring and removal criteria.

## 32. Architectural invariants

1. Version `0` is invalid for accepted communication contracts.
2. Version numbers and numeric identifiers are never reused for different meaning.
3. Firmware version is not a substitute for schema or protocol version.
4. Unsupported versions are rejected before state mutation.
5. Unknown state-changing fields are rejected by default.
6. Units and scales never change silently within one version.
7. MQTT/HTTP fallback preserves common record identity and meaning.
8. Protocol adapters do not silently downgrade or discard unsupported fields.
9. Persistent-layout versions remain separate from communication-schema versions.
10. Mixed-version compatibility is demonstrated by tests, not assumed from numeric ordering.
11. Removal follows deprecation and deployment evidence.
12. Negotiation cannot bypass authentication, authorization, or security policy.

## 33. Initial proposed decisions

This document proposes the following baseline for review:

| ID | Proposed decision |
|---|---|
| `COMM-VERS-001` | Use independent version domains rather than one global protocol version |
| `COMM-VERS-002` | Use nonzero monotonically allocated `uint16 schema_version` values |
| `COMM-VERS-003` | Reject unknown state-changing fields by default |
| `COMM-VERS-004` | Permit unknown optional telemetry/status fields only under bounded extension rules |
| `COMM-VERS-005` | Keep firmware, storage-layout, configuration, calibration, and communication versions separate |
| `COMM-VERS-006` | Require explicit compatibility matrices for mixed-version releases |
| `COMM-VERS-007` | Preserve common record identity and semantics across MQTT/HTTP fallback |
| `COMM-VERS-008` | Require a startup compatibility handshake for STM32–nRF52810 transport |
| `COMM-VERS-009` | Prohibit automatic protocol downgrade for state-changing operations |
| `COMM-VERS-010` | Maintain a normative registry for every accepted version domain |

## 34. Open decisions

| Decision | Question | Affected documents |
|---|---|---|
| MQTT application-version location | Payload field, versioned topic, or both with precedence | MQTT namespace/message catalog |
| HTTP version mechanism | Versioned path, media type, or header | HTTP API contract |
| BLE version exposure | Dedicated characteristic, command response, or both | BLE GATT contract |
| Capability encoding | Bitmap, numeric list, string identifiers, or channel-specific mappings | All protocol documents |
| Common schema transformation | Which adjacent versions may transform without semantic loss | Common data and test vectors |
| Supported overlap window | Minimum device/server/mobile compatibility duration | Release/deployment policy |
| Rollback policy | Supported rollback combinations and migration tooling | Firmware/storage documents |
| Version registry location | Markdown owner tables plus optional machine-readable registry | Documentation architecture |
| Deprecation telemetry | Which status/diagnostic fields report deprecated use | Common data/status catalog |

## 35. Definition of ready

This document may move from `Proposed` to `Accepted` when:

- initial proposed decisions are reviewed and entered in the project decision registry;
- common schema version 1 is formally allocated;
- MQTT, HTTP, BLE, and internal-link documents select their version exposure mechanisms;
- command/configuration unknown-field rules are confirmed;
- mixed-version compatibility and rollback responsibilities have owners;
- version-related error codes have a normative catalog location;
- Linux compatibility tests are planned with canonical fixtures;
- and no open decision changes the meaning of already accepted wire or persistent contracts.
