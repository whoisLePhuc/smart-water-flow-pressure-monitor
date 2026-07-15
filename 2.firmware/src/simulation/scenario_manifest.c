#include "simulation/scenario_manifest.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Simple JSON parser for scenario manifest v1.
 * Supports the minimal schema needed for initial scenario catalog.
 * Not a general-purpose JSON parser — no nesting, no arrays of objects. */

/* Trim leading whitespace */
static const char* skip_space(const char *p)
{
    while (p && *p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;
    return p;
}

/* Parse a string field: "key": "value" */
static const char* parse_string(const char *p, char *out, uint16_t max_len)
{
    p = skip_space(p);
    if (*p != '"') return NULL;
    p++;
    uint16_t i = 0;
    while (*p && *p != '"' && i < max_len - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    if (*p != '"') return NULL;
    p++;
    return p;
}

/* Parse a uint32 field */
static const char* parse_u32(const char *p, uint32_t *out)
{
    p = skip_space(p);
    char *end = NULL;
    *out = (uint32_t)strtoul(p, &end, 10);
    return (const char*)end;
}

/* Parse a uint64 field */
static const char* parse_u64(const char *p, uint64_t *out)
{
    p = skip_space(p);
    char *end = NULL;
    *out = (uint64_t)strtoull(p, &end, 10);
    return (const char*)end;
}

/* Match a key string */

bool manifest_parse(const char *json, ScenarioManifest *out,
                     char *error_msg, uint16_t error_max)
{
    if (!json || !out) return false;
    memset(out, 0, sizeof(*out));
    if (error_msg) error_msg[0] = '\0';

    const char *p = skip_space(json);
    if (*p != '{') {
        snprintf(error_msg, error_max, "Expected '{' at start");
        return false;
    }
    p++;

    while (p && *p && *p != '}') {
        p = skip_space(p);

        /* Read key */
        char key[64];
        p = parse_string(p, key, sizeof(key));
        if (!p) {
            snprintf(error_msg, error_max, "Failed to parse key");
            return false;
        }
        p = skip_space(p);
        if (*p != ':') {
            snprintf(error_msg, error_max, "Expected ':' after key '%s'", key);
            return false;
        }
        p++;

        if (strcmp(key, "scenario_name") == 0) {
            p = parse_string(p, out->scenario_name, MANIFEST_NAME_LEN);
        } else if (strcmp(key, "schema_version") == 0) {
            p = parse_u32(p, &out->schema_version);
        } else if (strcmp(key, "seed") == 0) {
            p = parse_u32(p, &out->seed);
        } else if (strcmp(key, "max_turns") == 0) {
            p = parse_u32(p, &out->max_turns);
        } else if (strcmp(key, "max_actions") == 0) {
            p = parse_u32(p, &out->max_actions);
        } else if (strcmp(key, "max_time_us") == 0) {
            p = parse_u64(p, &out->max_time_us);
        } else {
            /* Unknown key — skip value */
            p = skip_space(p);
            if (*p == '"') {
                while (*p && *p != '"') p++;
                if (*p) p++;
            } else if (*p >= '0' && *p <= '9') {
                while (*p && *p >= '0' && *p <= '9') p++;
            } else if (*p == '{' || *p == '[') {
                /* Skip objects and arrays — basic brace counting */
                char br = *p;
                char match = (br == '{') ? '}' : ']';
                int depth = 1;
                p++;
                while (p && *p && depth > 0) {
                    if (*p == br) depth++;
                    if (*p == match) depth--;
                    p++;
                }
            }
        }

        if (!p) {
            snprintf(error_msg, error_max, "Parse error after '%s'", key);
            return false;
        }

        p = skip_space(p);
        if (*p == ',') p++;
    }

    if (p && *p == '}') p++;

    /* Set defaults */
    if (out->schema_version == 0) out->schema_version = 1;
    if (out->max_turns == 0) out->max_turns = 500;
    if (out->max_actions == 0) out->max_actions = 16;
    if (out->max_time_us == 0) out->max_time_us = 10000000;

    return true;
}

bool manifest_validate(const ScenarioManifest *manifest,
                        char *error_msg, uint16_t error_max)
{
    if (!manifest) {
        if (error_msg) snprintf(error_msg, error_max, "NULL manifest");
        return false;
    }

    if (manifest->scenario_name[0] == '\0') {
        if (error_msg) snprintf(error_msg, error_max, "Missing scenario_name");
        return false;
    }

    if (manifest->schema_version != 1) {
        if (error_msg) snprintf(error_msg, error_max, "Unsupported schema version");
        return false;
    }

    if (manifest->num_actions > MANIFEST_MAX_ACTIONS) {
        if (error_msg) snprintf(error_msg, error_max, "Too many actions");
        return false;
    }

    if (manifest->num_peers > MANIFEST_MAX_PEERS) {
        if (error_msg) snprintf(error_msg, error_max, "Too many peers");
        return false;
    }

    /* Validate peer uniqueness */
    for (uint32_t i = 0; i < manifest->num_peers; i++) {
        for (uint32_t j = i + 1; j < manifest->num_peers; j++) {
            if (manifest->peers[i].i2c_address == manifest->peers[j].i2c_address &&
                manifest->peers[i].type == manifest->peers[j].type) {
                if (error_msg)
                    snprintf(error_msg, error_max, "Duplicate peer at address 0x%02x",
                             manifest->peers[i].i2c_address);
                return false;
            }
        }
    }

    return true;
}
