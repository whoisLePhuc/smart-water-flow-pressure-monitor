# Communication Documentation

> **Document ID:** COMM-INDEX
> **Status:** Draft
> **Baseline:** MVP communication-contract workspace
> **Applies to:** STM32L433RCT6 production firmware, nRF52810 BLE firmware, EC200U-CN integration, mobile application, and remote services

## 1. Purpose

This directory defines the communication architecture and the externally observable contracts of the Smart Water Flow and Pressure Monitor.

The documents specify:

* which communication channel is used for each system responsibility;
* which messages, commands, topics, endpoints, services, and characteristics exist;
* how application data is encoded and versioned;
* how identity, authorization, timeout, retry, duplication, and recovery are handled;
* how the STM32 exchanges commands and events with the nRF52810 and EC200U-CN;
* and how each contract is verified before hardware integration.

These documents are the normative input for communication implementation. Datasheets and vendor manuals remain reference material; they do not replace product-specific contracts.

## 2. System context

The communication subsystem connects the main firmware to local and remote consumers without making measurement logic depend on a particular protocol.

```mermaid
flowchart LR
    APP["Mobile application"] <-->|"BLE GATT"| NRF["nRF52810"]
    NRF <-->|"Internal transport"| MCU["STM32L433RCT6"]
    MCU <-->|"AT-command integration"| MODEM["EC200U-CN"]
    MODEM <-->|"MQTT / HTTPS"| CLOUD["Remote services"]
```

The main firmware publishes stable domain data such as `RuntimeSnapshot`, telemetry records, events, diagnostics, and command results. A transport-neutral remote-delivery layer selects MQTT or HTTP according to the accepted delivery policy. Protocol-specific modules then map the common records to MQTT, HTTP, BLE, or an internal transport representation.

Measurement, volume accumulation, leak detection, calibration, and persistence must remain usable when BLE or cellular connectivity is unavailable.

## 3. Channel responsibilities

| Channel         | Primary responsibility                                                                                            | MVP position                  | Explicit boundary                                                                                         |
| --------------- | ----------------------------------------------------------------------------------------------------------------- | ----------------------------- | --------------------------------------------------------------------------------------------------------- |
| MQTT            | Scheduled telemetry, important events, device status, and approved remote commands                                | Remote data-delivery channel  | MQTT delivery acknowledgement is not automatically an application-processing acknowledgement              |
| HTTP/HTTPS      | Telemetry, event, status, and diagnostic upload; may also support bounded provisioning or registration operations | Remote data-delivery channel  | HTTP success, partial acceptance, timeout, and idempotency semantics must be defined at application level |
| BLE GATT        | Local commissioning, controlled configuration, service access, and diagnostics                                    | Required local channel        | Defines the mobile-to-nRF52810 contract, not the STM32 byte-level link                                    |
| STM32–nRF52810  | Commands, responses, BLE events, and configuration transfer between the two firmware images                       | Required internal link        | Defined separately from BLE GATT because it is an internal transport contract                             |
| STM32–EC200U-CN | Modem power control, AT-command transactions, network registration, and MQTT/HTTP session control                 | Required cellular integration | Vendor AT commands are adapted into bounded product-level operations and events                           |

## 4. Architectural rules

The communication design shall follow these rules:

1. Protocol modules consume validated domain models and shall not own measurement algorithms.
2. Telemetry and display consumers read a stable published snapshot rather than partially updated measurement state.
3. Invalid or stale measurement data shall retain its quality information and shall not be silently presented as valid.
4. Communication work shall be non-blocking or explicitly bounded so it remains compatible with the cooperative event-driven runtime.
5. Interrupt handlers and receive callbacks shall capture minimal data and publish events; parsing and policy decisions belong outside ISR context.
6. Configuration writes shall pass validation, persistent commit, and controlled runtime-apply rules before success is reported.
7. Retry behavior shall be bounded. A communication outage must not create an unbounded queue or prevent measurement operation.
8. Duplicate delivery shall be expected wherever the underlying protocol can redeliver a message.
9. Wire contracts shall carry an explicit version or have a documented version-negotiation rule.
10. Credentials and secret material shall never appear in diagnostic logs, telemetry payloads, or test vectors.
11. Vendor-specific modem and BLE APIs shall not leak into portable domain or repository contracts.
12. New protocols or remote operations require an updated contract and traceable validation before production use.
13. Telemetry queues shall store transport-neutral application records, not MQTT packets or prebuilt HTTP requests.
14. A record shall retain the same stable `record_id` when retried or transferred through another remote channel.
15. Cross-channel fallback or parallel delivery shall not create unbounded duplicate uploads; server-side and device-side deduplication behavior must be explicit.
16. MQTT and HTTP delivery health shall be evaluated independently, while the remote-delivery service owns the final delivery state.

## 5. Directory structure

```text
03_communication/
├── README.md
├── 00_communication_architecture.md
├── 01_common_data_contract.md
├── 02_protocol_versioning.md
├── 03_security_and_identity.md
├── 04_error_retry_and_timeout_policy.md
├── 05_remote_delivery_policy.md
├── mqtt/
│   ├── README.md
│   ├── mqtt_connection_and_session.md
│   ├── mqtt_topic_namespace.md
│   ├── mqtt_message_catalog.md
│   └── mqtt_test_vectors.md
├── http/
│   ├── README.md
│   ├── http_api_contract.md
│   ├── http_delivery_and_batching.md
│   ├── http_message_catalog.md
│   └── http_test_vectors.md
├── ble/
│   ├── README.md
│   ├── ble_gatt_contract.md
│   ├── ble_command_catalog.md
│   ├── ble_security_and_pairing.md
│   └── ble_test_vectors.md
└── internal_links/
    ├── stm32_nrf52810_transport.md
    └── stm32_ec200u_integration.md
```

## 6. Document map

### 6.1 Common foundation

| Document                               | Responsibility                                                                                                      | Depends on                                           |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| `00_communication_architecture.md`     | Actors, channel roles, data flow, ownership, runtime interaction, and MVP boundaries                                | System architecture and accepted decisions           |
| `01_common_data_contract.md`           | Protocol-independent data models, field types, units, scaling, ranges, quality, and serialization rules             | Runtime data model and repositories                  |
| `02_protocol_versioning.md`            | Schema evolution, compatibility, unsupported-version behavior, and migration policy                                 | Common data contract                                 |
| `03_security_and_identity.md`          | Device identity, authentication, authorization, credential lifecycle, and security boundaries                       | Hardware identity and product provisioning decisions |
| `04_error_retry_and_timeout_policy.md` | Shared timeout, retry, backoff, duplicate, queue, expiration, and recovery rules                                    | Runtime timing and storage constraints               |
| `05_remote_delivery_policy.md`         | MQTT/HTTP selection, primary/fallback behavior, record lifecycle, delivery state, deduplication, and channel health | Common data contract and error/retry policy          |

### 6.2 MQTT

| Document                              | Responsibility                                                                               |
| ------------------------------------- | -------------------------------------------------------------------------------------------- |
| `mqtt/README.md`                      | MQTT scope, terminology, and reading order                                                   |
| `mqtt/mqtt_connection_and_session.md` | Broker connection, TLS, session, Keep Alive, Last Will, subscription, and reconnect behavior |
| `mqtt/mqtt_topic_namespace.md`        | Topic hierarchy, direction, ownership, QoS, retained flag, and authorization                 |
| `mqtt/mqtt_message_catalog.md`        | Telemetry, events, status, command requests, command results, and payload semantics          |
| `mqtt/mqtt_test_vectors.md`           | Canonical payloads, negative cases, retransmission, offline queue, and recovery tests        |

### 6.3 HTTP

| Document                             | Responsibility                                                                                                              |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------- |
| `http/README.md`                     | HTTP data-delivery scope, terminology, and relationship with MQTT                                                           |
| `http/http_api_contract.md`          | Base URL policy, telemetry/event/status endpoints, methods, headers, authentication, status codes, timeout, and idempotency |
| `http/http_delivery_and_batching.md` | Single-record and batch upload, ordering, size limits, partial success, retry, deduplication, and memory constraints        |
| `http/http_message_catalog.md`       | Telemetry, event, status, diagnostic, and other approved request/response schemas                                           |
| `http/http_test_vectors.md`          | Successful upload, partial acceptance, duplication, authentication, timeout, network, TLS, and server-error cases           |

### 6.4 BLE

| Document                          | Responsibility                                                                         |
| --------------------------------- | -------------------------------------------------------------------------------------- |
| `ble/README.md`                   | BLE scope, roles, and boundary from the internal nRF52810 link                         |
| `ble/ble_gatt_contract.md`        | Services, characteristics, UUIDs, permissions, MTU, fragmentation, and notifications   |
| `ble/ble_command_catalog.md`      | Local commands, validation, persistence, runtime apply, responses, and error semantics |
| `ble/ble_security_and_pairing.md` | Advertising, commissioning, pairing, bonding, authorization, and reset behavior        |
| `ble/ble_test_vectors.md`         | GATT, command, security, fragmentation, interruption, and recovery cases               |

### 6.5 Internal links

| Document                                     | Responsibility                                                                                                |
| -------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| `internal_links/stm32_nrf52810_transport.md` | Byte-level framing, command/event exchange, ACK/NACK, CRC, sequencing, timeout, startup, and recovery         |
| `internal_links/stm32_ec200u_integration.md` | Modem lifecycle, UART ownership, AT transactions, URCs, network registration, MQTT/HTTP control, and recovery |

## 7. Normative document boundary

Information belongs in this directory when it defines behavior visible across a communication boundary or a policy shared by multiple communication implementations.

Information belongs elsewhere when it concerns:

| Content                                                                              | Owning documentation                                             |
| ------------------------------------------------------------------------------------ | ---------------------------------------------------------------- |
| Measurement, compensation, volume, leak, calibration, and domain validity rules      | Firmware measurement and service documents                       |
| Event loop, FSM, repositories, queue ownership, and portable interfaces              | `1.docs/05_firmware/`                                            |
| MCU pins, UART instances, voltage levels, reset wiring, and power rails              | Hardware documentation                                           |
| Complete register descriptions and vendor command references                         | Datasheets and component references                              |
| Cloud deployment addresses, production secrets, and environment-specific credentials | Protected deployment configuration, not repository documentation |

Protocol documents may reference these sources but shall not duplicate their full content. Any duplicated summary must identify one normative owner.

## 8. Contract specification rules

Every message, command, endpoint, characteristic, or internal frame shall define, as applicable:

* stable name and identifier;
* direction and responsible producer/consumer;
* trigger and preconditions;
* field type, unit, scale, range, optionality, and byte order;
* encoding, maximum size, and fragmentation behavior;
* schema or protocol version;
* timeout, acknowledgement, retry, duplicate, and idempotency semantics;
* authorization requirement;
* success, rejection, and error behavior;
* persistence and runtime-apply effects;
* privacy or security classification;
* and at least one valid and one invalid test vector.

Examples are informative unless explicitly marked as canonical test vectors. A payload example shall not introduce fields that are absent from the corresponding field table.

## 9. Implementation relationship

The intended dependency direction is:

```text
Domain models and repositories
    -> transport-neutral telemetry queue
    -> RemoteDeliveryService and delivery policy
    -> MQTT or HTTP protocol mapping and state machines
    -> transport interfaces
    -> Linux simulation or STM32 platform adapters
    -> nRF52810 / EC200U-CN / network
```

The Linux implementation may emulate transport timing, responses, disconnections, malformed input, duplicate delivery, cross-channel fallback, partial HTTP batch acceptance, and recovery. Replacing a Linux adapter with an STM32 HAL adapter shall not require changes to the common data contract or protocol semantics.

The reporting scheduler shall submit application records to `RemoteDeliveryService`; it shall not invoke an MQTT- or HTTP-specific service directly. The delivery service owns channel selection and final delivery state, while MQTT and HTTP adapters own only their protocol-specific transactions.

## 10. Recommended implementation order

Develop and review the documents in this order:

1. `00_communication_architecture.md`
2. `01_common_data_contract.md`
3. `02_protocol_versioning.md`
4. `03_security_and_identity.md`
5. `04_error_retry_and_timeout_policy.md`
6. `05_remote_delivery_policy.md`
7. MQTT documentation and test vectors.
8. HTTP documentation and test vectors.
9. BLE documentation and test vectors.
10. STM32–nRF52810 and STM32–EC200U-CN internal-link contracts.

Protocol implementation shall not begin from message examples alone. The relevant common policies, message catalog, and minimum test vectors must be sufficiently defined first.

## 11. Definition of ready for implementation

A communication feature is ready for implementation when:

* its actors, direction, and responsibility are unambiguous;
* all wire-visible fields have types, units, ranges, and encoding rules;
* version and unsupported-version behavior are defined;
* timeout, retry, duplicate, and recovery behavior are bounded;
* authorization and credential assumptions are recorded;
* application acknowledgement is distinguished from transport acknowledgement where necessary;
* channel-selection, fallback, and cross-channel duplicate behavior are explicit;
* HTTP single-record or batch success and partial-acceptance semantics are explicit;
* persistence and runtime-apply side effects are explicit;
* invalid, stale, unavailable, and partial data behavior is specified;
* deterministic happy-path and fault-path test vectors exist;
* and unresolved decisions do not change the proposed wire contract.

## 12. Document status and change control

Use the following lifecycle:

| Status       | Meaning                                                                                  |
| ------------ | ---------------------------------------------------------------------------------------- |
| `Draft`      | Structure or content is incomplete and must not be treated as an implementation baseline |
| `Proposed`   | Contract is complete enough for review but still contains unaccepted decisions           |
| `Accepted`   | Contract is approved as an implementation and validation baseline                        |
| `Superseded` | A newer documented contract replaces this version                                        |

Any breaking change to an accepted wire contract requires:

1. a recorded decision and rationale;
2. an updated versioning impact assessment;
3. updated message catalogs and test vectors;
4. compatibility or migration behavior;
5. and traceability to affected firmware, mobile, cloud, or coprocessor implementations.

## 13. Current baseline and open decisions

The current project baseline assumes:

* MQTT and HTTP are both supported remote data-delivery channels over EC200U-CN.
* Telemetry, events, device status, and approved diagnostics may be delivered through either MQTT or HTTP according to the accepted remote-delivery policy.
* BLE through nRF52810 is used for local controlled configuration and service.
* Communication is scheduled and event-driven; communication failure does not invalidate local measurement operation.
* Remote OTA and unrestricted remote configuration are outside the current MVP unless separately accepted.
* The telemetry queue and record lifecycle are transport-neutral.

The following decisions must be resolved in the detailed documents:

* MQTT version, broker session policy, QoS per topic, retained messages, and application acknowledgements.
* Whether MQTT/HTTP selection is fixed by firmware variant, runtime configurable, primary/fallback, or parallel for specific record classes.
* Fallback trigger, retry budget, recovery-to-primary behavior, and channel-health criteria.
* Cross-channel deduplication and stable `record_id` behavior.
* HTTP endpoint layout, single-record versus batch upload, maximum payload size, partial success, and idempotency.
* JSON, CBOR, or another payload representation and numeric scaling rules.
* Device identity and credential provisioning flow.
* Offline queue capacity, expiration, prioritization, and persistence policy.
* Approved remote command set, if any, for MVP.
* BLE GATT UUID allocation, pairing method, commissioning window, and authorization levels.
* STM32–nRF52810 physical transport and frame format.
* EC200U-CN MQTT/HTTP operating mode and AT-command sequences.
* Which record classes may use HTTP and whether each class permits fallback or parallel delivery.

## 14. Related documentation

Before completing a protocol contract, review the relevant sections of:

* system overview, product scope, and accepted-decision registry;
* `1.docs/05_firmware/` for runtime, data ownership, persistence, scheduling, power, and service contracts;
* hardware documentation for STM32, nRF52810, EC200U-CN, UART, reset, power, and low-power constraints;
* measurement documents for units, ranges, validity, quality flags, and `RuntimeSnapshot` fields;
* and simulation documentation for deterministic fakes, fault injection, traces, and acceptance tests.

If two documents define conflicting values, do not silently choose one. Record the conflict, identify the normative owner, and resolve it through the project decision process.
