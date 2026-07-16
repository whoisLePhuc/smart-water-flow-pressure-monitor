#!/bin/bash
# check_architecture.sh — Architecture rule enforcement
# Exit 0 = pass, non-zero = violations found
#
# Usage:
#   bash scripts/check_architecture.sh [--warn|--enforce]
#
# Environment:
#   ARCH_ALLOWLIST — path to allowlist file (default: scripts/architecture_allowlist.txt)

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$FIRMWARE_DIR/src"

ENFORCE_MODE="${1:---warn}"
ALLOWLIST_FILE="${ARCH_ALLOWLIST:-$SCRIPT_DIR/architecture_allowlist.txt}"

VIOLATIONS=0
ERRORS=0
WARNINGS=0

# Colors for output
RED='\033[0;31m'
YELLOW='\033[0;33m'
GREEN='\033[0;32m'
NC='\033[0m'

# Check functions — all return number of violations found

check_domain_no_infrastructure() {
    local violations=0
    local pattern='#include.*infrastructure/'
    if [ -d "$SRC_DIR/domain" ]; then
        while IFS=: read -r file line content; do
            # Extract relative path for allowlist matching
            rel_file="${file#$FIRMWARE_DIR/}"
            if grep -q "R01 | $rel_file " "$ALLOWLIST_FILE" 2>/dev/null; then
                echo -e "  ${YELLOW}WARNING${NC} (allowlisted): $rel_file:$line — domain includes infrastructure header"
                ((WARNINGS++))
            else
                echo -e "  ${RED}ERROR${NC}: $rel_file:$line — domain MUST NOT include infrastructure headers"
                ((violations++))
                ((ERRORS++))
            fi
        done < <(grep -rn "$pattern" "$SRC_DIR/domain" --include='*.h' --include='*.c' 2>/dev/null)
    fi
    return $violations
}

check_domain_no_platform() {
    local violations=0
    local pattern='#include.*platform/'
    if [ -d "$SRC_DIR/domain" ]; then
        while IFS=: read -r file line content; do
            rel_file="${file#$FIRMWARE_DIR/}"
            if grep -q "R02 | $rel_file " "$ALLOWLIST_FILE" 2>/dev/null; then
                echo -e "  ${YELLOW}WARNING${NC} (allowlisted): $rel_file:$line — domain includes platform header"
                ((WARNINGS++))
            else
                echo -e "  ${RED}ERROR${NC}: $rel_file:$line — domain MUST NOT include platform/ headers"
                ((violations++))
                ((ERRORS++))
            fi
        done < <(grep -rn "$pattern" "$SRC_DIR/domain" --include='*.h' --include='*.c' 2>/dev/null)
    fi
    return $violations
}

check_domain_no_drivers() {
    local violations=0
    local pattern='#include.*drivers/'
    if [ -d "$SRC_DIR/domain" ]; then
        while IFS=: read -r file line content; do
            rel_file="${file#$FIRMWARE_DIR/}"
            if grep -q "R03 | $rel_file " "$ALLOWLIST_FILE" 2>/dev/null; then
                echo -e "  ${YELLOW}WARNING${NC} (allowlisted): $rel_file:$line — domain includes driver header"
                ((WARNINGS++))
            else
                echo -e "  ${RED}ERROR${NC}: $rel_file:$line — domain MUST NOT include driver headers"
                ((violations++))
                ((ERRORS++))
            fi
        done < <(grep -rn "$pattern" "$SRC_DIR/domain" --include='*.h' --include='*.c' 2>/dev/null)
    fi
    return $violations
}

check_no_new_global_accessors() {
    local violations=0
    # Check for new default/global accessor functions in production source (not tests)
    while IFS=: read -r file line content; do
        rel_file="${file#$FIRMWARE_DIR/}"
        # Skip files that are known to have existing globals
        if grep -q "G01 | $rel_file " "$ALLOWLIST_FILE" 2>/dev/null; then
            echo -e "  ${YELLOW}WARNING${NC} (allowlisted): $rel_file:$line — global accessor pattern"
            ((WARNINGS++))
        else
            echo -e "  ${RED}ERROR${NC}: $rel_file:$line — new global/default accessor function detected"
            ((violations++))
            ((ERRORS++))
        fi
    done < <(grep -rn '^\s*static.*default_\|_get_default\|static.*global_' "$SRC_DIR" \
        --include='*.c' --include='*.h' 2>/dev/null | grep -v '/tests/' | grep -v 'allowlisted')
    return $violations
}

check_no_new_legacy_includes() {
    local violations=0
    local pattern='#include.*data_model_legacy'
    local src_dir_new="$SRC_DIR/services $SRC_DIR/domain $SRC_DIR/protocols 2>/dev/null"
    for dir in "$SRC_DIR/services" "$SRC_DIR/domain" "$SRC_DIR/protocols"; do
        [ -d "$dir" ] || continue
        while IFS=: read -r file line content; do
            rel_file="${file#$FIRMWARE_DIR/}"
            echo -e "  ${YELLOW}WARNING${NC}: $rel_file:$line — new code should not include legacy header"
            ((violations++))
            ((WARNINGS++))
        done < <(grep -rn "$pattern" "$dir" --include='*.h' --include='*.c' 2>/dev/null)
    done
    return $violations
}

# ── Print header ──
echo "=== Firmware Architecture Check ==="
echo "Mode: $ENFORCE_MODE"
echo "Source: $SRC_DIR"
echo "Allowlist: $ALLOWLIST_FILE"
echo ""

# ── Run all checks ──
echo "--- Domain Layer Checks ---"
check_domain_no_infrastructure; VIOLATIONS=$((VIOLATIONS + $?))
check_domain_no_platform;      VIOLATIONS=$((VIOLATIONS + $?))
check_domain_no_drivers;       VIOLATIONS=$((VIOLATIONS + $?))

echo ""
echo "--- Cross-Cutting Checks ---"
check_no_new_global_accessors; VIOLATIONS=$((VIOLATIONS + $?))
check_no_new_legacy_includes;  VIOLATIONS=$((VIOLATIONS + $?))

# ── Summary ──
echo ""
echo "=== Summary ==="
echo "Errors:   $ERRORS"
echo "Warnings: $WARNINGS"
echo "Total violations: $VIOLATIONS"

if [ "$ENFORCE_MODE" = "--enforce" ] && [ "$ERRORS" -gt 0 ]; then
    echo -e "${RED}FAIL: $ERRORS error(s) found — fix before merge${NC}"
    exit 1
elif [ "$ENFORCE_MODE" = "--enforce" ]; then
    echo -e "${GREEN}PASS: No architecture violations${NC}"
    exit 0
else
    echo "Warning mode — violations reported but not enforced"
    exit 0
fi
