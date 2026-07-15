# MQTT Topic Namespace

| Metadata | Value |
| --- | --- |
| Document ID | COMM-MQTT-TOPIC-001 |
| Status | Proposed |
| Baseline | Versioned, device-scoped MQTT topic hierarchy |
| Applies to | STM32 firmware, EC200U integration, MQTT broker, remote services, and Linux simulation |

## 1. Purpose

This document defines the MQTT topic hierarchy used by the Smart Water Flow and Pressure Monitor. It specifies topic naming, direction, ownership, authorization boundaries, QoS and retained-message defaults, versioning, and wildcard use.

This document does not define the fields inside a payload. Payload schemas, triggers, application acknowledgements, and duplicate-processing rules are defined in [mqtt_message_catalog.md](mqtt_message_catalog.md).

## 2. Scope

This document covers:

- the topic root and topic segments;
- device-to-service and service-to-device directions;
- the proposed topic catalog;
- permitted publishers and subscribers;
- default QoS and retained-message behavior;
- broker access-control requirements;
- topic version migration;
- validation rules and examples.

It does not define:

- broker address, TLS, Client ID, Keep Alive, or session lifecycle;
- JSON, CBOR, or binary payload representation;
- semantic measurement fields;
- HTTP endpoints or BLE characteristics;
- broker deployment and administration procedures.

## 3. Normative language

The terms **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** express requirement strength.

Requirements marked **Proposed** remain the project baseline until reviewed against the selected broker, backend routing, device provisioning, and EC200U-CN firmware limits.

## 4. Design principles

The topic namespace MUST:

1. separate deployment environments;
2. expose a major MQTT contract version;
3. scope device traffic by stable `device_id`;
4. make message direction explicit;
5. support least-privilege broker authorization;
6. remain independent of MQTT Client ID and secret credentials;
7. avoid embedding record identifiers or rapidly changing values in the topic;
8. permit backend aggregation using controlled wildcards;
9. keep application payload meaning in the message catalog rather than encoding it into an excessively deep topic tree.

## 5. Proposed root pattern

The proposed MQTT topic pattern is:

```text
swfpm/{environment}/v{mqtt_contract_major}/devices/{device_id}/{direction}/{message_class}
```

Example:

```text
swfpm/prod/v1/devices/WM-000001/up/telemetry
```

### 5.1 Segment definitions

| Segment | Meaning | Proposed rule |
| --- | --- | --- |
| `swfpm` | Product namespace | Fixed lowercase literal |
| `{environment}` | Deployment boundary | Provisioned allowlisted value such as `dev`, `staging`, or `prod` |
| `v{mqtt_contract_major}` | Major MQTT topic/message contract | Lowercase `v` followed by a positive decimal integer |
| `devices` | Device resource collection | Fixed lowercase literal |
| `{device_id}` | Stable application device identity | Same semantic identity as the common data contract |
| `{direction}` | Message flow relative to the device | Exactly `up` or `down` |
| `{message_class}` | Logical message family | One value from the catalog in this document |

`up` means device to remote service. `down` means remote service to device.

### 5.2 Why direction is explicit

An explicit direction segment makes broker ACLs, routing, logs, and tests easier to review. A device can be permitted to publish only under its `up` branch and subscribe only under its `down` branch.

## 6. Topic catalog

### 6.1 Device-to-service topics

| Message class | Topic suffix | Publisher | Consumer | Default QoS | Retained | MVP |
| --- | --- | --- | --- | ---: | ---: | ---: |
| Telemetry | `up/telemetry` | Device | Ingestion service | 1 | No | Yes |
| Event | `up/events` | Device | Event service | 1 | No | Yes |
| Device status/presence | `up/status` | Device or MQTT Last Will | Status service | 1 | Conditional | Yes |
| Diagnostic | `up/diagnostics` | Device | Diagnostic service | 1 | No | Conditional |
| Command result | `up/command-results` | Device | Command service | 1 | No | When commands enabled |
| Configuration result | `up/configuration-results` | Device | Configuration service | 1 | No | When remote configuration enabled |

Full examples for device `WM-000001` in production contract version 1:

```text
swfpm/prod/v1/devices/WM-000001/up/telemetry
swfpm/prod/v1/devices/WM-000001/up/events
swfpm/prod/v1/devices/WM-000001/up/status
swfpm/prod/v1/devices/WM-000001/up/diagnostics
swfpm/prod/v1/devices/WM-000001/up/command-results
swfpm/prod/v1/devices/WM-000001/up/configuration-results
```

### 6.2 Service-to-device topics

| Message class | Topic suffix | Publisher | Consumer | Default QoS | Retained | MVP |
| --- | --- | --- | --- | ---: | ---: | ---: |
| Command request | `down/commands` | Command service | Device | 1 | No | Conditional approved set |
| Configuration request | `down/configurations` | Configuration service | Device | 1 | No | Conditional |
| Application receipt | `down/receipts` | Ingestion service | Device | 1 | No | If required by message policy |

Full examples:

```text
swfpm/prod/v1/devices/WM-000001/down/commands
swfpm/prod/v1/devices/WM-000001/down/configurations
swfpm/prod/v1/devices/WM-000001/down/receipts
```

The device MUST subscribe only to downlink message classes enabled by its product configuration and authorization policy.

## 7. Topic ownership

| Branch | Device permission | Remote-service permission |
| --- | --- | --- |
| `.../{device_id}/up/#` | Publish only | Subscribe/consume; administrative publishing prohibited by default |
| `.../{device_id}/down/#` | Subscribe only | Publish only from authorized services |

The normal device credential MUST NOT be allowed to:

- publish to another device's branch;
- subscribe to another device's branch;
- publish under its own `down` branch;
- subscribe to the environment-wide `#` hierarchy;
- create or use an unapproved message class;
- use wildcard characters in a publish topic.

Remote services SHOULD receive only the minimum topic branches required by their role. A command-producing service does not automatically need telemetry publishing or broker-administration rights.

## 8. Device identity rules

`{device_id}` is the stable identity defined by [../01_common_data_contract.md](../01_common_data_contract.md).

The topic `device_id`:

- MUST match the authenticated device identity provisioned in the broker ACL;
- MUST match the `device_id` inside an application payload when that field is present;
- MUST remain stable across reboot, MQTT reconnect, HTTP fallback, SIM replacement, and normal firmware update;
- MUST NOT be a password, token, private key, IMSI, MQTT Packet Identifier, or volatile MQTT Client ID;
- MUST NOT be selected from untrusted payload data without identity validation.

A mismatch between authenticated identity, topic identity, and payload identity MUST be rejected and recorded as a security or contract violation.

## 9. Naming and encoding rules

### 9.1 General segment rules

Topic segments defined by this contract MUST:

- use UTF-8 strings accepted by MQTT and the selected broker;
- use lowercase ASCII for fixed contract literals;
- use `/` only as the segment delimiter;
- contain no empty segment;
- contain no leading or trailing `/`;
- contain no whitespace or control character;
- contain neither `+` nor `#` in a concrete publish or device subscription topic;
- remain within the verified modem, broker, and firmware topic-length limits.

Fixed message-class names use lowercase kebab-case, for example `command-results`.

### 9.2 Proposed `environment` format

`{environment}` MUST be provisioned from an allowlist. Proposed values are:

```text
dev
staging
prod
```

A production credential MUST NOT be authorized for a development or staging branch, or vice versa.

### 9.3 Proposed `device_id` topic format

Until product identity rules are finalized, a topic-safe `device_id` SHOULD match:

```regex
^[A-Za-z0-9][A-Za-z0-9._-]{0,47}$
```

This preserves the common-contract maximum of 48 characters and excludes topic delimiters and wildcard characters. The exact character set remains a validation gate.

If a future `device_id` contains characters outside the approved topic-safe set, the system MUST define one deterministic reversible encoding. Implementations MUST NOT independently invent different escaping schemes.

## 10. Versioning policy

`v{mqtt_contract_major}` is the major version of the MQTT application contract, not the MQTT protocol version and not the common payload `schema_version`.

These are separate version domains:

| Version | Example | Owns |
| --- | --- | --- |
| MQTT protocol version | MQTT 3.1.1 | Wire protocol and session behavior |
| MQTT contract major | `v1` topic segment | Topic/message compatibility boundary |
| Common schema version | `schema_version: 1` | Semantic record fields and meaning |

A compatible additive payload change SHOULD keep the same MQTT topic-major version and change the payload schema according to [../02_protocol_versioning.md](../02_protocol_versioning.md).

A new MQTT topic-major version is required when an incompatible change cannot be safely handled inside the existing namespace, including:

- changing the meaning or direction of an existing topic;
- changing a topic from one logical message family to another;
- removing behavior required by existing consumers;
- changing authorization boundaries incompatibly;
- changing acknowledgement semantics in a way that can cause false delivery completion.

During migration, the backend MAY support two major roots concurrently. A device MUST publish a given logical record through only the selected contract version unless an explicit migration policy defines otherwise. Dual publishing MUST preserve `(device_id, record_id)` and remote deduplication.

## 11. QoS policy

QoS in this document is the default topic-level policy. The final requirement for each message belongs in [mqtt_message_catalog.md](mqtt_message_catalog.md).

Proposed rules:

- contract topics use QoS 1 by default;
- QoS 2 is not used in the MVP;
- QoS 0 MAY be introduced only for explicitly lossy, non-critical diagnostics;
- a subscription request SHOULD request QoS 1 for downlink topics;
- the device MUST handle a broker-granted subscription QoS lower than requested according to the message policy;
- MQTT acknowledgement MUST NOT automatically be treated as application processing unless the message catalog says it is sufficient.

QoS retransmission can produce duplicates. Consumers MUST use application identities such as `(device_id, record_id)` and `command_id`, not the MQTT Packet Identifier, for deduplication.

## 12. Retained-message policy

Retained messages are disabled by default.

| Topic class | Retained policy | Reason |
| --- | --- | --- |
| Telemetry | MUST NOT retain | A newly subscribed consumer must not confuse an old sample with current telemetry |
| Events | MUST NOT retain | Event delivery and replay use record identity and backend persistence |
| Diagnostics | MUST NOT retain | Diagnostic data may be stale or sensitive |
| Commands | MUST NOT retain | Prevent unintended execution of stale commands |
| Configurations | MUST NOT retain in MVP | Remote configuration requires explicit identity, freshness, result, and replay handling |
| Receipts | MUST NOT retain | Receipts correlate with queued application records |
| Command/configuration results | MUST NOT retain | Backend persistence owns result history |
| Status | MAY retain | Only after freshness and unexpected-disconnect semantics are defined |

If retained status is enabled:

- the payload MUST include version, device identity, state, and an event/observation timestamp with time-quality semantics;
- consumers MUST treat it as the most recently reported state, not proof that the device is currently online;
- the Last Will and graceful status behavior MUST follow [mqtt_connection_and_session.md](mqtt_connection_and_session.md);
- the system MUST define how an obsolete retained status is cleared or superseded.

## 13. Last Will topic

When Last Will is enabled, it uses the device status topic:

```text
swfpm/{environment}/v{mqtt_contract_major}/devices/{device_id}/up/status
```

The Last Will payload MUST use the status schema defined in the message catalog and indicate an unexpected MQTT disconnection. It MUST NOT use a separate undocumented topic.

The broker MUST validate that the authenticated device is allowed to configure a Last Will only within its own permitted `up/status` topic.

## 14. Wildcard policy

Wildcards are intended for authorized remote-service subscriptions and controlled tests. They MUST NOT appear in a PUBLISH topic.

Examples of backend subscription filters:

```text
swfpm/prod/v1/devices/+/up/telemetry
swfpm/prod/v1/devices/+/up/events
swfpm/prod/v1/devices/+/up/status
```

An ingestion service MAY use:

```text
swfpm/prod/v1/devices/+/up/#
```

only if it is authorized and designed to validate every matching message class.

The following broad filter SHOULD be reserved for administration and integration testing:

```text
swfpm/prod/v1/#
```

A normal device MUST subscribe using its exact provisioned topic paths, not `+` or `#`.

## 15. Subscription set

After every successful clean MQTT connection, the device restores the downlink topics required by enabled features.

| Feature | Required subscription |
| --- | --- |
| Remote commands | `.../{device_id}/down/commands` |
| Remote configuration | `.../{device_id}/down/configurations` |
| Application receipts | `.../{device_id}/down/receipts` |

The MQTT transport enters `READY` only after every mandatory subscription is acknowledged, as defined in [mqtt_connection_and_session.md](mqtt_connection_and_session.md).

If none of these features is enabled, no downlink subscription is required and MQTT MAY operate as a publish-only delivery channel.

## 16. Message routing rules

The MQTT adapter MUST choose the topic from the common record type and the approved message catalog. Application code MUST NOT construct arbitrary topic strings from payload contents.

Proposed outbound mapping:

| Common record | Message class |
| --- | --- |
| `TelemetryRecord` | `telemetry` |
| `EventRecord` | `events` |
| `DeviceStatusRecord` | `status` |
| `DiagnosticRecord` | `diagnostics` |
| `CommandResultRecord` | `command-results` |
| `ConfigurationResultRecord` | `configuration-results` |

The exact record names and mappings remain governed by [../01_common_data_contract.md](../01_common_data_contract.md) and [mqtt_message_catalog.md](mqtt_message_catalog.md).

`record_id`, `command_id`, timestamps, measurement type, event code, and firmware version MUST be carried in the payload when required. They MUST NOT be appended as dynamic topic segments in the MVP.

## 17. Topic construction and validation

The implementation SHOULD construct topics using validated segments and fixed templates rather than general string concatenation.

Before using a topic, the MQTT adapter MUST verify:

1. the environment is provisioned and allowlisted;
2. the MQTT contract major is supported;
3. the `device_id` is present and topic-safe;
4. direction and message class form an approved combination;
5. the final encoded length fits the local buffer, modem, and broker limits;
6. no wildcard, empty segment, control character, or unintended delimiter exists;
7. the operation is permitted for the authenticated device role.

An invalid topic is a configuration or contract error. It MUST NOT be retried as a transient network failure.

## 18. Valid examples

Assume environment `prod`, MQTT contract major `1`, and device `WM-000001`.

| Topic | Use |
| --- | --- |
| `swfpm/prod/v1/devices/WM-000001/up/telemetry` | Device publishes a telemetry record |
| `swfpm/prod/v1/devices/WM-000001/up/events` | Device publishes an event record |
| `swfpm/prod/v1/devices/WM-000001/up/status` | Device or Last Will publishes status |
| `swfpm/prod/v1/devices/WM-000001/down/commands` | Device subscribes for approved commands |
| `swfpm/prod/v1/devices/WM-000001/up/command-results` | Device publishes command results |
| `swfpm/prod/v1/devices/+/up/telemetry` | Authorized backend subscribes to production telemetry |

## 19. Invalid examples

| Topic | Reason |
| --- | --- |
| `/swfpm/prod/v1/devices/WM-000001/up/telemetry` | Leading slash creates an unintended empty segment |
| `swfpm/prod/v1/devices/WM-000001/up/telemetry/` | Trailing slash creates an empty segment |
| `swfpm/PROD/v1/devices/WM-000001/up/telemetry` | Fixed/provisioned environment value is not canonical |
| `swfpm/prod/v1/devices/WM-000001/telemetry` | Direction segment is missing |
| `swfpm/prod/v1/devices/WM-000001/down/telemetry` | Invalid direction for telemetry |
| `swfpm/prod/v1/devices/+/up/telemetry` published by a device | Wildcard is invalid for publish and device identity is not exact |
| `swfpm/prod/v1/devices/WM-000002/up/events` published by `WM-000001` | Authenticated identity and topic identity differ |
| `swfpm/prod/v1/devices/WM-000001/down/commands/1234` | Dynamic command ID segment is not part of the MVP contract |
| `swfpm/prod/v1/devices/WM-000001/up/battery` | Battery is a payload field or approved record mapping, not an ad hoc topic |

The last example is important: adding a battery-energy field to a common record does not create a new MQTT topic. It changes the common data contract and MQTT payload mapping unless battery data is deliberately introduced as a new record/message class.

## 20. Security and privacy

Topics are routing metadata and may be visible in broker logs. Therefore, topics MUST NOT contain:

- passwords, access tokens, certificate material, or private keys;
- personal names, addresses, phone numbers, or free-form user data;
- raw SIM credentials or unnecessary modem identifiers;
- measurement values, alarm text, or unrestricted diagnostics.

TLS and broker authentication remain mandatory as defined in [../03_security_and_identity.md](../03_security_and_identity.md). Topic naming does not replace authorization.

## 21. Observability

Logs MAY record the canonical topic or a sanitized topic identifier when needed for integration diagnostics. Production logging SHOULD avoid creating unnecessary long-term identity trails.

At minimum, diagnostics SHOULD distinguish:

- invalid topic construction;
- ACL publish rejection;
- subscription rejection;
- unsupported topic-major version;
- unknown message class;
- topic/payload identity mismatch;
- retained-policy violation.

Credentials and secret authentication material MUST never be logged with the topic.

## 22. Change-control rules

| Proposed change | Required action |
| --- | --- |
| Add a payload field such as battery energy | Update common data contract and message catalog; normally no topic change |
| Add a new message type within an existing compatible class | Update message catalog; retain topic when routing and ACL semantics are unchanged |
| Add a new message class | Add an explicit topic entry, ACL, payload contract, and tests |
| Rename or repurpose a topic | Treat as potentially breaking; evaluate a new MQTT contract major |
| Change QoS or retained behavior | Update this document and message catalog; assess duplicate/stale-message impact |
| Enable commands/configuration | Review security, ACL, subscription readiness, payload schema, replay policy, and tests |
| Add tenant routing | Define a reviewed namespace migration; do not insert an ad hoc segment |

## 23. Verification requirements

Tests MUST cover at least:

1. construction of every catalog topic;
2. maximum permitted `device_id` length;
3. rejected slash, wildcard, whitespace, control character, and empty segment;
4. device publish allowed only under its own `up` branch;
5. device subscribe allowed only under its own enabled `down` topics;
6. cross-device publish and subscribe rejection;
7. environment isolation;
8. backend wildcard routing by message class;
9. Last Will authorization on the status topic;
10. retained-message rejection for telemetry, events, commands, and receipts;
11. unsupported contract-major rejection;
12. topic/payload `device_id` mismatch rejection;
13. unknown message-class rejection;
14. MQTT reconnect and restoration of the exact mandatory subscription set.

Canonical cases belong in [mqtt_test_vectors.md](mqtt_test_vectors.md).

## 24. Validation gates and open decisions

The following items remain **Proposed**:

- final product root literal `swfpm`;
- whether deployment environments share a broker and therefore require the environment segment;
- final environment allowlist;
- exact `device_id` topic character set;
- maximum complete topic length supported by STM32 buffers, EC200U-CN firmware, and broker;
- broker ACL syntax and credential-to-device binding;
- whether retained status is enabled;
- whether application receipts are required and for which record classes;
- approved command and remote-configuration scope;
- whether future multi-tenant routing requires a new namespace version.

After these values are verified with firmware, broker, backend, and security owners, this document SHOULD move from `Proposed` to `Approved`.

## 25. References

- [mqtt_connection_and_session.md](mqtt_connection_and_session.md)
- [mqtt_message_catalog.md](mqtt_message_catalog.md)
- [../01_common_data_contract.md](../01_common_data_contract.md)
- [../02_protocol_versioning.md](../02_protocol_versioning.md)
- [../03_security_and_identity.md](../03_security_and_identity.md)
- [../04_error_retry_and_timeout_policy.md](../04_error_retry_and_timeout_policy.md)
- [../05_remote_delivery_policy.md](../05_remote_delivery_policy.md)
