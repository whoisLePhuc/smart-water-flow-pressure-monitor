# Security and Identity

| Metadata | Value |
|---|---|
| Document ID | `COMM-SEC-003` |
| Status | `Proposed` |
| Baseline | Unique device identity, authenticated encrypted remote channels, authorized BLE commissioning, bounded credential lifecycle |
| Applies to | STM32 firmware, nRF52810 firmware, EC200U-CN integration, mobile application, MQTT/HTTP services, manufacturing, provisioning, and service workflows |

## 1. Purpose

This document defines security and identity requirements for the communication subsystem of the Smart Water Flow and Pressure Monitor.

It specifies:

- identities and principals used by the product;
- trust boundaries and threat assumptions;
- authentication and authorization responsibilities;
- credential provisioning, storage, use, rotation, revocation, and destruction;
- MQTT, HTTP/HTTPS, BLE, and internal-link security requirements;
- command replay and duplicate protection;
- security-event reporting and privacy constraints;
- factory, field-service, reset, and decommissioning behavior;
- and validation gates required before production deployment.

This document defines product security policy. It does not contain production secrets, private keys, passwords, tokens, certificate files, or deployment-specific credential values.

## 2. Security objectives

The communication system shall provide:

1. unique and stable device identification;
2. remote-server authentication before protected data exchange;
3. device/client authentication appropriate to each channel;
4. confidentiality and integrity for remote telemetry and commands;
5. authorization before any state-changing operation;
6. replay and duplicate protection for commands and configuration;
7. bounded exposure if one credential is compromised;
8. secure credential lifecycle and controlled reset/decommissioning;
9. separation between device identity, protocol session identity, and credentials;
10. diagnosable security failures without exposing secrets;
11. continued local measurement during remote authentication or network failure;
12. deterministic negative-path testing in Linux before hardware validation.

## 3. Security references and interpretation

This project uses the following sources as design references:

- IETF TLS specifications for authenticated, confidential, integrity-protected client/server channels;
- NIST IR 8259A as an IoT device cybersecurity capability baseline;
- Bluetooth SIG security and privacy guidance for selection and use of BLE security features.

These sources do not automatically determine the complete product configuration. EC200U-CN capabilities, nRF52810 firmware, mobile application, cloud infrastructure, manufacturing process, and product threat model must be verified before this document becomes `Accepted`.

## 4. Scope

### 4.1 In scope

- Device and component identity.
- MQTT and HTTP/HTTPS authentication.
- BLE commissioning, pairing, bonding, and application authorization.
- STM32–nRF52810 trust and link-integrity boundary.
- STM32–EC200U-CN credential and modem-operation boundary.
- Credential storage and provisioning interfaces.
- Remote/local command authorization.
- Replay, duplicate, downgrade, and brute-force controls.
- Security diagnostics, audit metadata, and privacy.

### 4.2 Out of scope

- Cloud-infrastructure implementation details.
- Full public-key infrastructure operations manual.
- Physical enclosure certification.
- Complete secure-boot/OTA design unless added by a separate product decision.
- Source-code signing and release-infrastructure procedures.
- Legal privacy/compliance classification for a deployment jurisdiction.
- Production credentials or secret-generation scripts.

Out-of-scope items may still be prerequisites for a production security claim.

## 5. Trust boundaries

```mermaid
flowchart LR
    APP["Mobile application"] -->|"BLE security + app authorization"| NRF["nRF52810"]
    NRF -->|"Internal framed link"| STM["STM32 trust domain"]
    STM -->|"Controlled AT interface"| MODEM["EC200U-CN"]
    MODEM -->|"TLS"| CLOUD["Authorized remote service"]
```

| Boundary | Primary risks | Required controls |
|---|---|---|
| Mobile ↔ nRF52810 | Unauthorized configuration, passive observation, replay, untrusted app | BLE security, commissioning policy, authorization, rate limit, transaction identity |
| nRF52810 ↔ STM32 | Injected/corrupt frames, reset desynchronization, unauthorized command forwarding | Framing integrity, version handshake, allowlisted messages, bounded parser, state validation |
| STM32 ↔ EC200U-CN | AT injection, credential leakage, stale URC/response confusion | Serialized ownership, bounded command construction, response correlation, protected logs |
| Device ↔ MQTT/HTTP service | Server impersonation, eavesdropping, tampering, replay, credential theft | TLS, server validation, device authentication, authorization, record/command identity |
| Firmware ↔ persistent storage | Secret disclosure, corrupted config, rollback, unauthorized replacement | Access boundary, version/integrity checks, minimized secret storage, protected provisioning |
| Manufacturing/service tools ↔ device | Credential duplication, unauthorized provisioning, overbroad service access | Authenticated tools, per-device provisioning, audit, least privilege, controlled lifecycle |

## 6. Threat assumptions

The design assumes an attacker may:

- observe, delay, duplicate, reorder, drop, or inject network traffic;
- attempt to impersonate a broker, HTTP service, mobile application, or device;
- replay previously captured commands or configuration requests;
- send malformed, oversized, unsupported, or conflicting payloads;
- repeatedly attempt BLE pairing or protected writes;
- obtain a non-secret device identifier;
- trigger power loss or reset during communication/provisioning;
- exploit mixed firmware/protocol versions;
- obtain diagnostic logs produced by normal service workflows.

The current baseline does not claim resistance to an attacker with unlimited invasive physical access, semiconductor-level key extraction, or compromised manufacturing/cloud infrastructure. Those risks require hardware and operational controls beyond this communication contract.

## 7. Security identities

### 7.1 Identity types

| Identity | Scope | Stability | Secret? |
|---|---|---|---:|
| `device_id` | Product/application identity | Device lifetime | No |
| `serial_number` | Manufacturing/service identity | Device lifetime | No |
| MQTT client ID | MQTT session/routing identity | Provisioned or derived | No by itself |
| HTTP device principal | HTTP authentication/authorization identity | Credential lifetime | Identifier is not necessarily secret |
| Credential ID/key ID | Selects credential material | Credential lifetime | No by itself |
| Private key/PSK/token | Authentication secret | Credential lifetime | Yes |
| SIM/network identity | Cellular network identity | SIM/subscription lifetime | Sensitive; exposure restricted |
| BLE device identity/bond | BLE peer relationship | Bond lifetime | Keys are secret |
| Mobile/service principal | Human/tool/application authorization identity | Account/session lifetime | Authentication material is secret |
| Firmware variant | Product capability identity | Build/release lifetime | No |
| Sensor profile ID | Measurement configuration identity | Profile lifetime | Normally no |

### 7.2 Identity invariants

1. `device_id` shall remain stable across reboot, protocol switch, network change, and normal firmware update.
2. `device_id` shall not be a password, private key, token, or direct encoding of a secret.
3. Serial number, IMEI/SIM identity, BLE address, MQTT client ID, and `device_id` shall not be treated as interchangeable.
4. Authentication proves control of a credential; an identifier alone does not authenticate a principal.
5. One device shall not share a device-authentication secret with the whole fleet unless a separately accepted risk decision allows it.
6. Server-side authorization shall bind authenticated credentials to the expected `device_id` and permitted operations.

## 8. Identity provisioning

### 8.1 Manufacturing provisioning

The proposed production flow is:

```text
allocate device identity
    -> generate or assign per-device credential
    -> provision through authenticated factory tool
    -> verify read-back through non-secret metadata or proof-of-possession
    -> bind identity and credential in backend inventory
    -> lock/close provisioning interface
    -> record auditable result without secret material
```

### 8.2 Provisioning requirements

- Credentials shall be unique per device where supported by the selected authentication scheme.
- Secret generation shall use an approved cryptographic random source.
- Secrets shall not be transmitted through ordinary debug logs or repository files.
- Factory tools shall authenticate and apply least privilege.
- A failed/partial provisioning transaction shall leave a diagnosable state and shall not silently mark the device production-ready.
- Reprovisioning shall require an explicitly authorized service/factory mode.
- Backend registration and device provisioning shall have a recoverable reconciliation process.

### 8.3 Provisioning state

| State | Meaning | Remote behavior |
|---|---|---|
| `UNPROVISIONED` | No valid production identity/credential | Production MQTT/HTTP disabled |
| `PROVISIONING` | Controlled provisioning transaction active | Only provisioning operations allowed |
| `PROVISIONED` | Valid credential and backend binding present | Normal authorized communication allowed |
| `CREDENTIAL_EXPIRED` | Credential no longer valid by policy | Telemetry queued; controlled recovery required |
| `CREDENTIAL_REVOKED` | Backend/device marks credential unusable | Authentication attempts stopped or heavily bounded |
| `SECURITY_LOCKED` | Repeated failure or integrity issue requires service | Protected operations denied according to recovery policy |

## 9. Credential classes

| Credential class | Candidate use | Notes |
|---|---|---|
| Device client certificate/private key | MQTT and/or HTTPS mutual authentication | Strong per-device identity; requires certificate lifecycle and protected private key |
| Per-device PSK | TLS-PSK or application authentication where supported | Operationally simpler but requires secure PSK distribution and isolation |
| Short-lived access token | HTTP or MQTT session authorization | Requires secure bootstrap/refresh and trusted time or bounded lifetime handling |
| Server CA/trust anchor | Server certificate validation | Public but integrity-critical |
| BLE bond keys | Reconnect authentication/encryption | Managed by nRF52810 BLE stack/storage |
| Commissioning proof | Initial local authorization | Must be unique/bounded and protected from reuse |

The final credential scheme remains open until EC200U-CN TLS storage/API capability, backend infrastructure, manufacturing flow, memory budget, and rotation requirements are verified.

## 10. Credential storage

### 10.1 Storage principles

1. Store the minimum secret material required on each component.
2. Prefer non-exportable modem/secure-element key storage if available and verified.
3. Do not assume FM24CL04B F-RAM provides confidentiality or tamper resistance.
4. Integrity-protected storage is not automatically confidential storage.
5. Plaintext secrets shall not be placed in ordinary config records without an accepted threat/risk decision.
6. References/handles to modem-held credentials are preferable to copying private keys into application buffers.
7. Secret buffers shall have bounded lifetime and be cleared when practical after use.
8. Crash traces, normalized traces, test fixtures, and unit-test logs shall use dummy credentials only.

### 10.2 Proposed ownership

| Material | Preferred owner |
|---|---|
| Device non-secret identity | STM32 configuration/identity repository |
| Server trust anchor | EC200U-CN trusted storage or verified protected firmware storage |
| Device private key/PSK | Non-exportable modem/secure hardware storage when available |
| MQTT/HTTP credential reference | STM32 security configuration repository |
| BLE bond keys | nRF52810 BLE stack persistent storage |
| Commissioning state | nRF52810 plus STM32 product authorization state as required |

Exact ownership is not `Accepted` until hardware capabilities and provisioning tests confirm it.

## 11. Credential lifecycle

Every production credential shall support a defined lifecycle:

```text
generated
  -> provisioned
  -> activated
  -> used
  -> rotated or renewed
  -> revoked/expired
  -> destroyed or decommissioned
```

### 11.1 Rotation

- Rotation shall be authenticated and authorized.
- Old and new credential overlap shall be bounded.
- Activation shall be atomic or recoverable after reset/power loss.
- Failure to activate a new credential shall not erase the last valid credential unless policy explicitly requires fail-closed behavior.
- Rotation responses shall not echo secret material.
- Backend and device state shall be reconcilable.

### 11.2 Revocation and expiry

- Revoked credentials shall not continue normal retry indefinitely.
- Authentication failure shall be distinguishable from temporary network failure.
- Expired credentials shall trigger bounded recovery/service behavior.
- A device with revoked remote credentials shall continue safe local measurement unless product policy requires another action.

## 12. Remote channel security baseline

MQTT and HTTP shall use authenticated encrypted transport for production remote communication.

### 12.1 TLS requirements

- TLS 1.3 is preferred where the modem and service support an accepted configuration.
- TLS 1.2 may be supported only as an explicitly reviewed compatibility profile with approved cipher suites and server validation.
- Obsolete SSL/TLS versions shall not be used.
- Server identity shall be validated against an approved trust anchor and expected service identity.
- Certificate-validity checks require a defined trusted-time policy.
- Hostname/service-name verification shall not be disabled merely to make testing pass.
- TLS errors shall fail closed for protected remote delivery.
- TLS 0-RTT/early data shall not carry state-changing commands or non-idempotent operations unless a separate anti-replay design is accepted.
- Session resumption shall not bypass current credential, authorization, or revocation policy.

### 12.2 Trust-anchor management

- Trust anchors are integrity-critical configuration.
- Replacement requires authenticated, authorized, versioned update.
- At least one recoverable transition strategy is required for CA rotation.
- Expired or unknown anchors shall produce explicit diagnostics.
- Test and production anchors shall be separated.

## 13. MQTT security

### 13.1 Authentication

MQTT connection shall authenticate the server through TLS. Device authentication shall use the selected per-device certificate, PSK, or bounded token scheme.

Username/password fields, if used, are credentials and shall not be logged or embedded in source.

### 13.2 Authorization

Broker/service authorization shall restrict each device to its allowed topic namespace and actions.

Minimum policy:

- device may publish only its permitted telemetry/event/status/result topics;
- device may subscribe only to its permitted command/control topics;
- one device shall not read or publish another device's namespace;
- retained command topics require explicit policy and expiration handling;
- wildcard subscriptions for production devices should be avoided unless narrowly justified.

### 13.3 MQTT message security

- QoS acknowledgement does not replace authentication or command authorization.
- Command payloads require `command_id`, schema version, and replay/duplicate checks.
- Retained commands shall not execute merely because the broker redelivers them after reconnect.
- Last Will payload shall contain no secret material.
- MQTT client ID shall not be treated as proof of identity.

## 14. HTTP/HTTPS security

### 14.1 Authentication and authorization

- HTTPS is required for production telemetry, event, status, diagnostic, provisioning, and command-related operations.
- Device authentication shall bind the request principal to the expected `device_id`.
- Endpoint authorization shall be least privilege by operation and record class.
- Access tokens, if used, shall be bounded in scope and lifetime.
- Refresh/bootstrap operations require a separately authenticated trust path.

### 14.2 Request security

- State-changing and upload retries use stable idempotency derived from the application record/request identity.
- Redirects to untrusted host, scheme downgrade, or unexpected API version shall be rejected.
- Request and response sizes are bounded before parsing/copying.
- Duplicate JSON fields and ambiguous encodings are rejected according to the common data/versioning contract.
- HTTP status alone does not authorize configuration or command execution.
- Server-provided retry instructions are bounded by device policy.

### 14.3 HTTP command ingress

If HTTP later supports commands through polling or response piggybacking:

- command content is authenticated as part of the TLS/server channel and application authorization;
- command identity, version, expiry, and duplicate rules still apply;
- a telemetry response shall not silently contain executable behavior outside the approved command envelope;
- redirects and cached responses shall not become command sources.

## 15. BLE security model

BLE security has two layers:

1. BLE link security managed by nRF52810 and the mobile peer.
2. Product-level authorization enforced before STM32 state mutation.

Encrypted BLE transport alone does not authorize every product command.

### 15.1 BLE operation classes

| Class | Examples | Minimum authorization intent |
|---|---|---|
| Public discovery | Product type, limited non-sensitive version | May be readable without pairing if privacy policy allows |
| Local status | Current status/measurement summary | Encrypted link or authorized session according to privacy assessment |
| Configuration read | Reporting/channel/config values | Authenticated/bonded or commissioning-authorized peer |
| Configuration write | Reporting interval, preferred channel, bounded settings | Authenticated encrypted link plus product authorization |
| Sensitive provisioning | Credential/trust configuration | Dedicated factory/service authorization; not ordinary user BLE |
| Destructive/reset command | Bond reset, credential erase, factory reset | Strong authorization and explicit confirmation policy |

### 15.2 Pairing and bonding

- LE Secure Connections should be used where supported by both peers and product I/O constraints.
- The selected association model shall be documented with its MITM properties.
- “Just Works” shall not be assumed sufficient for protected configuration merely because it encrypts the link.
- Bonding shall be limited to an accepted number of peers.
- Bond replacement and deletion behavior shall be deterministic.
- Pairing shall be allowed only during an explicit commissioning/service window unless an accepted usability/security policy states otherwise.
- Repeated failed pairing attempts shall be rate-limited and diagnosed without permanent denial-of-service from a small number of failures.

### 15.3 Commissioning window

The commissioning window shall define:

- authorized trigger, such as physical/local service action or factory state;
- start and maximum duration;
- advertising mode and exposed information;
- allowed operations;
- success and failure exit conditions;
- reboot behavior;
- rate limit and lockout recovery.

Remote commands shall not silently open an unrestricted local commissioning window unless separately authorized and logged.

### 15.4 Application authorization

Protected BLE commands require a product-level authorized session or equivalent proof linked to the current connection. Authorization expires on disconnect, timeout, reset, or security-state change as defined by the BLE security document.

## 16. STM32–nRF52810 internal-link security

The internal physical link is within the device enclosure but remains an input-validation boundary.

Requirements:

- version handshake before normal operation;
- framed messages with length and integrity check;
- allowlisted message and command identifiers;
- sequence/transaction correlation;
- bounded payload and parser state;
- reset/session resynchronization;
- unknown state-changing messages rejected;
- nRF52810 cannot directly mutate STM32 repositories;
- STM32 revalidates authorization-relevant command metadata;
- secret provisioning messages, if ever supported, require a dedicated authenticated factory/service design.

A CRC detects corruption but does not authenticate a malicious peer. If the physical threat model requires protection against an attached/replaced nRF52810, an authenticated internal-link mechanism or hardware trust control must be added by decision.

## 17. STM32–EC200U-CN security boundary

Requirements:

- one modem service owns AT-command serialization;
- command strings are built from allowlisted operations and validated parameters;
- untrusted remote payload is never concatenated into arbitrary AT commands;
- response and URC parsing is bounded and correlated to modem state;
- logs redact credentials, certificate contents, tokens, APN secrets, and sensitive SIM identifiers;
- modem credential references/slots are used instead of exporting secrets when supported;
- modem reset/recovery clears volatile authentication/session assumptions;
- unqualified modem firmware/security capability produces explicit degraded status.

## 18. Authorization model

### 18.1 Principals

Candidate principal classes:

| Principal | Typical channel | Intended authority |
|---|---|---|
| Device itself | MQTT/HTTP | Publish own records and receive allowed responses |
| User mobile session | BLE | Read status and change bounded user-configurable values |
| Service technician | BLE/service tool | Diagnostics and bounded service configuration |
| Factory station | Controlled provisioning link | Identity/credential/profile provisioning |
| Remote operations service | MQTT/HTTP | Approved remote commands and policy updates |
| Backend ingestion service | MQTT/HTTP | Accept device records; no device mutation authority |

### 18.2 Authorization decision inputs

Authorization may depend on:

- authenticated principal;
- channel and security state;
- command type;
- product/firmware variant;
- commissioning/service mode;
- current FSM state;
- physical presence requirement;
- credential status;
- command version and expiry;
- configuration key classification.

### 18.3 Least privilege

- Read and write permissions are separate.
- Telemetry publication credential need not authorize configuration.
- BLE user role shall not provision production remote secrets by default.
- Service access shall not imply factory credential authority.
- Diagnostics shall expose only data necessary for the role.

## 19. Command and configuration security

The protected command pipeline is:

```text
receive bounded message
  -> verify channel security/authentication
  -> decode and validate schema
  -> identify principal
  -> authorize command and fields
  -> check version, expiry, replay, duplicate, and current-state guard
  -> validate domain constraints
  -> commit persistent change if required
  -> apply at safe runtime boundary
  -> emit bounded result and security audit metadata
```

Failure at any step prevents later side effects.

### 19.1 Sensitive configuration classes

| Class | Examples | Default handling |
|---|---|---|
| User configuration | Reporting interval, display/local settings | BLE-authorized bounded update |
| Delivery policy | Preferred MQTT/HTTP channel, fallback enable | Authorized update with validation and persistence |
| Deployment network | APN, server/broker profile | Service/factory-controlled |
| Trust configuration | CA/trust anchor | Strong service/factory authorization |
| Authentication secret | Private key, PSK, token/bootstrap credential | Protected provisioning only; never echoed |
| Calibration/profile | Sensor calibration/profile | Factory/service workflow with domain validation |

## 20. Replay and duplicate protection

### 20.1 Outbound records

- `(device_id, record_id)` identifies one immutable outbound record.
- Retry or MQTT/HTTP fallback preserves `record_id`.
- Remote ingestion shall deduplicate without accepting a different payload under the same ID.

### 20.2 Commands

- Commands require stable `command_id` scoped to the authenticated origin/device relationship.
- Duplicate command delivery returns the previously known result or a bounded duplicate status; it does not execute again.
- Expiry is checked when trusted time is available and required.
- Where trusted wall clock is unavailable, nonce/sequence/session policies shall provide bounded replay protection.
- Replay state persistence shall be sufficient for commands whose effects must not repeat after reboot.

### 20.3 TLS early data

Replayable early-data modes shall not carry non-idempotent state-changing operations without an accepted anti-replay design.

## 21. Rate limiting and abuse resistance

Rate limits shall exist for:

- BLE pairing attempts;
- BLE authorization attempts;
- protected command attempts;
- credential/provisioning operations;
- authentication reconnect loops;
- malformed frame/message processing;
- security diagnostic emission.

Rate limiting shall be bounded and recoverable. An attacker shall not be able to force permanent lockout through a small number of unauthenticated requests unless a separately accepted fail-secure policy requires service recovery.

Backoff shall permit low-power operation during extended attack/failure conditions.

## 22. Factory reset and security reset

Reset semantics shall distinguish:

| Operation | Expected effect |
|---|---|
| Runtime restart | Clears volatile sessions; preserves provisioned identity/credentials |
| User configuration reset | Resets user-configurable values; normally preserves production identity/credentials |
| BLE bond reset | Deletes authorized BLE bonds and local authorization state |
| Network profile reset | Removes selected deployment network settings under service authorization |
| Security credential reset | Revokes/erases designated credentials; requires strong authorization |
| Full decommission/factory reset | Removes customer/site data and credentials according to lifecycle policy |

“Factory reset” shall not be one ambiguous command. Each effect, authorization level, physical-presence requirement, acknowledgement, persistence behavior, and recovery path must be explicit.

## 23. Decommissioning

Decommissioning should:

- revoke backend credentials/binding;
- delete or invalidate device authentication secrets;
- remove BLE bonds and local authorization state;
- clear deployment-specific network/server configuration;
- clear customer/site-specific data according to retention policy;
- preserve only allowed manufacturing identity/service evidence;
- produce a bounded auditable result without secrets;
- prevent automatic reconnection using decommissioned credentials.

## 24. Logging, diagnostics, and audit

### 24.1 Allowed security metadata

- normalized event/error code;
- affected subsystem/channel;
- credential ID/key ID, not secret value;
- principal/role identifier when privacy policy permits;
- command ID/record ID;
- timestamp and time quality;
- attempt count and outcome;
- firmware/protocol/security-profile version.

### 24.2 Prohibited log content

- private keys, PSKs, tokens, passwords;
- BLE bond keys or commissioning proof;
- full authentication headers;
- unredacted credential-bearing AT commands;
- raw certificate private material;
- arbitrary memory dumps;
- production secret test vectors.

### 24.3 Security event categories

The diagnostic catalog shall include stable categories for:

- authentication failed;
- authorization denied;
- unsupported/insecure protocol version;
- certificate/trust validation failed;
- credential expired/revoked/missing;
- replay or duplicate command detected;
- pairing/commissioning abuse threshold reached;
- malformed protected request;
- provisioning/rotation/reset failed;
- internal-link integrity/version failure;
- security storage/integrity failure.

## 25. Privacy requirements

- Collect and transmit only data required for product operation and diagnostics.
- Avoid exposing stable device identifiers in public BLE advertising unless required and assessed.
- BLE address/privacy behavior shall be selected with mobile interoperability requirements.
- Customer/site identity shall not be embedded into low-level diagnostic codes.
- SIM identifiers, network location, and signal metadata are sensitive and shall have explicit consumers.
- Retention and deletion responsibilities shall be assigned to device, mobile application, and backend owners.
- Debug/test environments shall not receive production telemetry or credentials unintentionally.

## 26. Time and security

Trusted time affects:

- certificate validity;
- token expiry;
- command expiry;
- audit ordering;
- credential rotation;
- replay windows.

Requirements:

1. Time quality shall be known before relying on wall-clock security checks.
2. Unsynchronized time shall not cause certificate validation to be silently disabled.
3. Bootstrap behavior for obtaining trusted time shall be explicitly designed.
4. Large backward/forward time changes shall invalidate or re-evaluate affected sessions.
5. Monotonic time shall govern local retry, authorization-session timeout, and commissioning duration.

## 27. Failure behavior

| Failure | Required high-level behavior |
|---|---|
| Server certificate invalid | Reject connection; no protected data exchange |
| Device authentication rejected | Stop tight retry; diagnose and enter bounded recovery/backoff |
| Credential missing/corrupt | Mark channel unavailable/security fault; preserve safe local operation |
| BLE authorization failure | Reject operation; no domain/persistent side effect |
| Replay/duplicate command | Do not re-execute; return known/bounded result when safe |
| Unknown security-affecting field | Reject entire state-changing request |
| Trust-anchor update interrupted | Recover old valid anchor or explicit service-required state |
| nRF link malformed/incompatible | Disable dependent BLE operations; measurement continues |
| Modem firmware unqualified | Disable unsupported secure operation or enter explicit degraded state |

Fail-open behavior for authentication, authorization, trust validation, or secret integrity is prohibited unless a specific accepted decision documents the safety and security tradeoff.

## 28. Linux simulation requirements

Use dummy test identities and credentials only.

Deterministic fakes shall support:

- valid and invalid server identity;
- missing, expired, revoked, and rotated device credential;
- MQTT authorization denied by topic/action;
- HTTP `401`/`403` and token refresh/bootstrap failure;
- BLE pairing/authorization success and failure;
- commissioning-window expiry;
- repeated-attempt rate limiting;
- duplicate and replayed command;
- reset during provisioning or rotation;
- trust-anchor version mismatch;
- unsynchronized time during certificate/token validation;
- secret-redaction assertions in logs/traces;
- nRF internal-link corruption and incompatible version.

Simulation shall validate product state transitions and event traces, not cryptographic algorithm correctness. Cryptographic libraries and modem TLS require their own integration/qualification tests.

## 29. Hardware and integration validation

Before production acceptance, verify on real hardware:

- EC200U-CN supported TLS versions, cipher suites, certificate validation, hostname handling, key/certificate storage, and error reporting;
- actual server trust and device-authentication flow;
- behavior with invalid/expired certificate and wrong hostname;
- credential provisioning, proof-of-possession, rotation, revocation, and power-loss recovery;
- nRF52810 pairing, bonding, LE Secure Connections capability, association model, privacy, and bond storage;
- commissioning-window and authorization UX;
- log redaction from STM32, nRF52810, modem, mobile app, and backend;
- memory, power, and timing impact of secure sessions;
- downgrade and mixed-version rejection;
- reset/decommissioning erase and backend revocation behavior.

## 30. Security test requirements

Tests shall cover:

- identifier is not accepted as authentication;
- one device cannot publish/read another device namespace;
- protected BLE write without authorization;
- unknown command field and unsupported command version;
- duplicate/replayed command before and after reboot;
- malformed/oversized authenticated payload;
- TLS/server-validation failure;
- credential expiry, revocation, rotation overlap, and recovery;
- interrupted trust-anchor update;
- commissioning brute-force/rate-limit behavior;
- unauthorized factory/service operation;
- security reset class authorization;
- MQTT retained stale command;
- HTTP redirect/scheme downgrade rejection;
- `record_id` deduplication across MQTT/HTTP;
- secret absence from logs, traces, crash reports, and test artifacts;
- local measurement continuity during remote security failure.

## 31. Security invariants

1. No production MQTT or HTTP data is exchanged without authenticated encrypted transport.
2. An identifier alone never proves identity.
3. State-changing operations require explicit authorization.
4. Authentication/authorization failure produces no partial domain or persistent side effects.
5. `record_id` and `command_id` semantics are preserved across channels.
6. Duplicate commands are not executed twice.
7. Secret material is absent from ordinary logs and communication records.
8. F-RAM integrity mechanisms are not claimed as secret confidentiality.
9. BLE link encryption alone does not grant unrestricted product authority.
10. CRC on an internal frame is not claimed as peer authentication.
11. Protocol downgrade cannot bypass active security policy.
12. Security failure does not silently disable certificate, authentication, or authorization checks.
13. Retry and rate-limit behavior remains bounded.
14. Remote security failure does not stop safe local measurement.

## 32. Initial proposed decisions

| ID | Proposed decision |
|---|---|
| `COMM-SEC-001` | Use unique stable `device_id` distinct from all authentication secrets and transport identifiers |
| `COMM-SEC-002` | Require authenticated encrypted transport for production MQTT and HTTP |
| `COMM-SEC-003` | Prefer TLS 1.3; allow TLS 1.2 only through an explicitly reviewed compatibility profile |
| `COMM-SEC-004` | Require server identity validation; prohibit disabling it as a deployment workaround |
| `COMM-SEC-005` | Use per-device remote credentials where selected infrastructure supports them |
| `COMM-SEC-006` | Treat FM24CL04B as non-confidential storage unless additional protection is accepted |
| `COMM-SEC-007` | Separate BLE link security from product-level command authorization |
| `COMM-SEC-008` | Restrict pairing/protected commissioning to a bounded authorized window |
| `COMM-SEC-009` | Reject unknown state-changing fields and prevent partial application |
| `COMM-SEC-010` | Use stable command identity and replay/duplicate protection |
| `COMM-SEC-011` | Redact all credential material from logs, traces, and diagnostics |
| `COMM-SEC-012` | Preserve local measurement operation during remote authentication/security failure |

## 33. Open decisions

| Decision | Question | Blocking impact |
|---|---|---|
| Remote credential scheme | Client certificate, per-device PSK, token bootstrap, or approved combination | Provisioning, modem storage, backend authentication |
| Private-key owner | EC200U non-exportable storage, secure element, STM32 protected storage, or other | Hardware/integration architecture |
| Trust-anchor rotation | Dual-anchor overlap, signed bundle, service reprovisioning | Certificate lifecycle and rollback |
| Trusted-time bootstrap | RTC provisioning, network source, authenticated bootstrap flow | Certificate/token validity checks |
| BLE association model | Numeric comparison, passkey, OOB, Just Works plus app proof, or service-specific design | Mobile UX and MITM resistance |
| Commissioning trigger | Physical input, factory state, local service action, or bounded combination | Hardware/UI and BLE policy |
| Bond policy | Maximum peers, replacement, owner transfer, reset behavior | nRF storage and mobile workflow |
| Product authorization | Bond-only, application proof/session, role token, or combination | BLE command contract |
| Security reset | Exact physical-presence and service authorization requirements | Command/configuration/reset design |
| Internal-link authentication | CRC-only corruption detection or authenticated link for stronger physical threat model | STM32–nRF framing and key provisioning |
| Remote commands | Allowed types, channel, role, expiry, and replay persistence | MQTT/HTTP catalogs and storage |
| Security audit retention | Which events persist and for how long within F-RAM limits | Storage and diagnostics |

## 34. Definition of ready

This document may move from `Proposed` to `Accepted` when:

- product threat model and physical-access assumptions are reviewed;
- EC200U-CN secure TLS and credential-storage capabilities are verified on hardware;
- remote device-authentication scheme is selected;
- manufacturing provisioning, backend binding, rotation, and revocation have owners;
- BLE association, bonding, commissioning, and product-authorization model are selected;
- trusted-time bootstrap and certificate/token behavior are accepted;
- sensitive configuration and reset authorization are mapped to command contracts;
- security event/error codes have a normative catalog;
- Linux negative-path tests and hardware qualification tests are planned;
- and every open decision affecting wire, storage, manufacturing, mobile, or backend contracts is accepted or explicitly deferred outside MVP.

## 35. References

- [RFC 9846 — The Transport Layer Security (TLS) Protocol Version 1.3](https://www.rfc-editor.org/info/rfc9846)
- [NIST IR 8259A — IoT Device Cybersecurity Capability Core Baseline](https://csrc.nist.gov/pubs/ir/8259/a/final)
- [Bluetooth SIG — Bluetooth Security](https://www.bluetooth.com/learn-about-bluetooth/key-attributes/bluetooth-security/)

Before this document becomes `Accepted`, references shall be checked for current revisions, updates, errata, and product-applicable Bluetooth/TLS guidance.
