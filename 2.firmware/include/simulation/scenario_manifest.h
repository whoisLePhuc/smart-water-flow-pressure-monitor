#ifndef SWFPM_SCENARIO_MANIFEST_H
#define SWFPM_SCENARIO_MANIFEST_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Scenario manifest v1 — strict UTF-8 JSON schema for deterministic scenarios.
 *
 * Top-level fields (canonical per 93_linux_simulation_integration.md):
 *   schema_version, scenario_name, seed, max_turns, max_actions, max_time_us,
 *   hardware_config { spi, i2c, gpio }, peers [], actions [], assertions []
 *
 * This header defines the parsed C structs. The JSON parser is in
 * scenario_manifest.c.
 */

#define MANIFEST_MAX_PEERS     8
#define MANIFEST_MAX_ACTIONS  64
#define MANIFEST_MAX_ASSERT   16
#define MANIFEST_NAME_LEN     64

typedef enum {
    PEER_TYPE_MAX35103,
    PEER_TYPE_ZSSC3241,
    PEER_TYPE_FRAM,
} PeerType;

typedef struct {
    PeerType type;
    uint8_t  i2c_address;  /* 0 for SPI devices */
    uint32_t fixture_id;
    uint32_t fault_plan;   /* 0 = normal */
} ManifestPeer;

typedef enum {
    ACTION_SCHEDULE_SPI,
    ACTION_SCHEDULE_I2C,
    ACTION_SCHEDULE_GPIO,
    ACTION_SCHEDULE_RESET,
    ACTION_ADVANCE_TIME,
    ACTION_POST_EVENT,
    ACTION_SET_FAULT,
    ACTION_ASSERT,
} ActionType;

typedef struct {
    ActionType type;
    uint64_t   at_us;        /* Absolute or relative time */
    uint32_t   resource_id;
    uint32_t   value;        /* Type-specific payload */
    uint32_t   duration_us;  /* For advance/assert actions */
} ManifestAction;

typedef enum {
    ASSERT_MODE_EQUALS,
    ASSERT_TRACE_CONTAINS,
    ASSERT_COUNTER_GT,
} AssertType;

typedef struct {
    AssertType type;
    uint32_t   target_id;
    uint32_t   expected_value;
} ManifestAssert;

typedef struct {
    char          scenario_name[MANIFEST_NAME_LEN];
    uint32_t      schema_version;
    uint32_t      seed;

    /* Run limits */
    uint32_t      max_turns;
    uint32_t      max_actions;
    uint64_t      max_time_us;

    /* Hardware configuration */
    uint32_t      num_spi_devices;
    uint32_t      num_i2c_peers;
    uint32_t      num_gpio_lines;

    /* Peers */
    uint32_t      num_peers;
    ManifestPeer  peers[MANIFEST_MAX_PEERS];

    /* Scheduled actions */
    uint32_t      num_actions;
    ManifestAction actions[MANIFEST_MAX_ACTIONS];

    /* Assertions */
    uint32_t      num_asserts;
    ManifestAssert asserts[MANIFEST_MAX_ASSERT];
} ScenarioManifest;

/* Parse a JSON string into a ScenarioManifest.
 * Returns true on success. On failure, error_msg is set. */
bool manifest_parse(const char *json, ScenarioManifest *out,
                     char *error_msg, uint16_t error_max);

/* Validate the manifest after parsing.
 * Checks: peer uniqueness, action bounds, time consistency. */
bool manifest_validate(const ScenarioManifest *manifest,
                        char *error_msg, uint16_t error_max);

#endif
