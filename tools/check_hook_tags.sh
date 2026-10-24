#!/usr/bin/env bash
#
# Hook-tag drift check for the game template.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMPLATE_FILES="${SCRIPT_DIR}/../src/shared/BasicGame/BasicGame.hpp ${SCRIPT_DIR}/../template/game/MyGame.cpp"
CORE_SRC="${1:-$(cd "${SCRIPT_DIR}/../.." && pwd)/Core/src}"
MANIFEST="${SCRIPT_DIR}/hook_tags.manifest"

for f in ${TEMPLATE_FILES}; do
    if [ ! -f "${f}" ]; then
        echo "check_hook_tags: template file not found: ${f}" >&2
        exit 1
    fi
done
if [ ! -d "${CORE_SRC}" ]; then
    echo "check_hook_tags: Core source dir not found: ${CORE_SRC}" >&2
    exit 1
fi
if [ ! -f "${MANIFEST}" ]; then
    echo "check_hook_tags: manifest not found: ${MANIFEST}" >&2
    exit 1
fi

MANIFEST_TAGS="$(grep -v '^[[:space:]]*#' "${MANIFEST}" | sed '/^[[:space:]]*$/d' | sort -u)"

TAGS="$(cat ${TEMPLATE_FILES} \
        | grep -oE '(\.hook\(|hook_if_set\(ctx, ?)"[a-z_0-9]+"' \
        | sed 's/.*"\(.*\)"/\1/' | sort -u)"

if [ -z "${TAGS}" ]; then
    echo "check_hook_tags: no hook tags found in ${TEMPLATE_FILES}" >&2
    exit 1
fi

FAIL=0

while IFS= read -r tag; do
    [ -n "${tag}" ] || continue
    if ! printf '%s\n' "${MANIFEST_TAGS}" | grep -qxF "${tag}"; then
        echo "check_hook_tags: FAIL - registered tag not in manifest: ${tag}" >&2
        FAIL=1
    fi
done <<< "${TAGS}"

while IFS= read -r tag; do
    [ -n "${tag}" ] || continue
    if ! grep -rqF "${tag}" "${CORE_SRC}" \
            --include='*.cpp' --include='*.h' --include='*.hpp'; then
        echo "check_hook_tags: FAIL - manifest tag not read by Core: ${tag}" >&2
        FAIL=1
    fi
done <<< "${MANIFEST_TAGS}"

while IFS= read -r tag; do
    [ -n "${tag}" ] || continue
    if ! grep -rqF "${tag}" "${CORE_SRC}" \
            --include='*.cpp' --include='*.h' --include='*.hpp'; then
        echo "check_hook_tags: FAIL - hook tag Core does not read: ${tag}" >&2
        FAIL=1
    fi
done <<< "${TAGS}"

if [ "${FAIL}" -ne 0 ]; then
    exit 1
fi

echo "check_hook_tags: OK - all hook tags consistent with manifest and Core"
