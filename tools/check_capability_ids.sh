#!/usr/bin/env bash
# Capability-id drift check (v3 P6).
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE_SRC="${1:-$(cd "${SCRIPT_DIR}/../.." && pwd)/Core/src}"
ABI_DIR="${2:-$(cd "${SCRIPT_DIR}/.." && pwd)/../ABI}"
MANIFEST="${SCRIPT_DIR}/capability_ids.manifest"

# Resolve ABI dir (standalone vs submodule).
if [ ! -d "${ABI_DIR}/include" ]; then
    for p in "${SCRIPT_DIR}/../../external/abi" "${SCRIPT_DIR}/../abi"; do
        if [ -d "${p}/include" ]; then ABI_DIR="${p}"; break; fi
    done
fi

if [ ! -f "${MANIFEST}" ]; then
    echo "check_capability_ids: manifest not found: ${MANIFEST}" >&2
    exit 1
fi
if [ ! -d "${CORE_SRC}" ]; then
    echo "check_capability_ids: Core source dir not found: ${CORE_SRC}" >&2
    exit 1
fi

MANIFEST_IDS="$(grep -v '^[[:space:]]*#' "${MANIFEST}" | sed '/^[[:space:]]*$/d' | sort -u)"

FAIL=0

# 1. Core capability_manifest.cpp ids must equal manifest.
CORE_MANIFEST_FILE="${CORE_SRC}/engine/game/registry/capability_manifest.cpp"
if [ ! -f "${CORE_MANIFEST_FILE}" ]; then
    CORE_MANIFEST_FILE="${CORE_SRC}/src/engine/game/registry/capability_manifest.cpp"
fi
if [ ! -f "${CORE_MANIFEST_FILE}" ]; then
    echo "check_capability_ids: Core manifest not found: ${CORE_MANIFEST_FILE}" >&2
    exit 1
fi
# Extract first quoted string on lines containing CapabilityKind.
CORE_IDS="$(grep 'CapabilityKind::' "${CORE_MANIFEST_FILE}" | grep -oE '"[a-z_.]+"' | tr -d '"' | sort -u)"

while IFS= read -r id; do
    [ -n "${id}" ] || continue
    if ! printf '%s\n' "${MANIFEST_IDS}" | grep -qxF "${id}"; then
        echo "check_capability_ids: FAIL - Core id not in manifest: ${id}" >&2
        FAIL=1
    fi
done <<< "${CORE_IDS}"

while IFS= read -r id; do
    [ -n "${id}" ] || continue
    if ! printf '%s\n' "${CORE_IDS}" | grep -qxF "${id}"; then
        echo "check_capability_ids: FAIL - manifest id not in Core: ${id}" >&2
        FAIL=1
    fi
done <<< "${MANIFEST_IDS}"

# 2. ABI GMM_CAPID_* defines must be subset of manifest.
ABI_HEADER="${ABI_DIR}/include/gmm_abi_v3.h"
if [ -f "${ABI_HEADER}" ]; then
    ABI_IDS="$(grep -oE '#define GMM_CAPID_[A-Z_]+ "[a-z_.]+"' "${ABI_HEADER}" | grep -oE '"[a-z_.]+"' | tr -d '"' | sort -u)"
    while IFS= read -r id; do
        [ -n "${id}" ] || continue
        if ! printf '%s\n' "${MANIFEST_IDS}" | grep -qxF "${id}"; then
            echo "check_capability_ids: FAIL - ABI id not in manifest: ${id}" >&2
            FAIL=1
        fi
    done <<< "${ABI_IDS}"
fi

if [ "${FAIL}" -ne 0 ]; then
    exit 1
fi

echo "check_capability_ids: OK - capability ids consistent with manifest"
