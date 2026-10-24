#!/usr/bin/env bash
# Method-id drift check (v3 P6).
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ABI_DIR="${1:-$(cd "${SCRIPT_DIR}/.." && pwd)/../ABI}"
SDK_DIR="${ABI_DIR}/sdk"
MANIFEST="${SCRIPT_DIR}/method_ids.manifest"

if [ ! -d "${ABI_DIR}/include" ]; then
    for p in "${SCRIPT_DIR}/../../external/abi" "${SCRIPT_DIR}/../abi"; do
        if [ -d "${p}/include" ]; then ABI_DIR="${p}"; SDK_DIR="${p}/sdk"; break; fi
    done
fi

if [ ! -f "${MANIFEST}" ]; then
    echo "check_method_ids: manifest not found: ${MANIFEST}" >&2
    exit 1
fi

MANIFEST_IDS="$(grep -v '^[[:space:]]*#' "${MANIFEST}" | sed '/^[[:space:]]*$/d' | sort -u)"

FAIL=0

ABI_HEADER="${ABI_DIR}/include/gmm_abi_v3.h"
if [ ! -f "${ABI_HEADER}" ]; then
    echo "check_method_ids: ABI header not found: ${ABI_HEADER}" >&2
    exit 1
fi

# ABI enum constants: identifiers GMM_* on lines with '=' inside enum.
ABI_IDS="$(grep -oE 'GMM_[A-Z_]+' "${ABI_HEADER}" | sort -u | grep -E 'GMM_(SAVES|OVERLAY|PREVIEW|ANIM|SORT|TOOL|STAGE|VALIDATE|EVENT|DIAGNOSE|ORDER)' )"

# Simpler: extract from enum lines.
ABI_IDS="$(grep 'enum { GMM_' "${ABI_HEADER}" | grep -v 'GMM_LOG_' | grep -oE 'GMM_[A-Z_]+' | sort -u)"

while IFS= read -r id; do
    [ -n "${id}" ] || continue
    if ! printf '%s\n' "${MANIFEST_IDS}" | grep -qxF "${id}"; then
        echo "check_method_ids: FAIL - ABI id not in manifest: ${id}" >&2
        FAIL=1
    fi
done <<< "${ABI_IDS}"

while IFS= read -r id; do
    [ -n "${id}" ] || continue
    if ! printf '%s\n' "${ABI_IDS}" | grep -qxF "${id}"; then
        echo "check_method_ids: FAIL - manifest id not in ABI: ${id}" >&2
        FAIL=1
    fi
done <<< "${MANIFEST_IDS}"

# SDK references: GMM_* constants used in sdk headers.
if [ -d "${SDK_DIR}/include" ]; then
    SDK_IDS="$(grep -rhoE 'GMM_[A-Z_]+' "${SDK_DIR}/include" | sort -u | grep -E 'GMM_(SAVES|OVERLAY|PREVIEW|ANIM|SORT|TOOL|STAGE|VALIDATE|EVENT|DIAGNOSE|ORDER)')"
    while IFS= read -r id; do
        [ -n "${id}" ] || continue
        if ! printf '%s\n' "${MANIFEST_IDS}" | grep -qxF "${id}"; then
            echo "check_method_ids: FAIL - SDK id not in manifest: ${id}" >&2
            FAIL=1
        fi
    done <<< "${SDK_IDS}"
fi

if [ "${FAIL}" -ne 0 ]; then
    exit 1
fi

echo "check_method_ids: OK - method ids consistent with manifest"
