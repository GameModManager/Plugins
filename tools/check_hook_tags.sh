#!/usr/bin/env bash
#
# Hook-tag drift check for the game template.
#
# Greps Core for every hook tag the template registers and FAILS listing
# tags Core does not read. Hook tags are stringly typed with no checked-in
# vocabulary, so a stale or hallucinated tag silently produces a dead hook
# (four such dead tags existed before this check). Run it after adding or
# changing hooks in the template - or in a copied game plugin.
#
# Usage:
#   tools/check_hook_tags.sh [CORE_SRC_DIR]
#
# Default CORE_SRC_DIR: the sibling Core checkout (projects/Core/src).
# The CMake test entry passes Core's resolved engine dir explicitly.
#
# Exit 0: every tag is read by Core. Exit 1: lists the unread tags.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# The hook tags live in the BasicGame base class (the descriptor-driven
# chain) plus any the template example adds via extra().
TEMPLATE_FILES="${SCRIPT_DIR}/../src/shared/BasicGame/BasicGame.hpp ${SCRIPT_DIR}/../template/game/MyGame.cpp"
CORE_SRC="${1:-$(cd "${SCRIPT_DIR}/../.." && pwd)/Core/src}"

for f in ${TEMPLATE_FILES}; do
    if [ ! -f "${f}" ]; then
        echo "check_hook_tags: template file not found: ${f}" >&2
        exit 1
    fi
done
if [ ! -d "${CORE_SRC}" ]; then
    echo "check_hook_tags: Core source dir not found: ${CORE_SRC}" >&2
    echo "  pass it explicitly: tools/check_hook_tags.sh /path/to/Core/src" >&2
    exit 1
fi

# Every data hook tag the template registers (registration lines only;
# commented-out examples are skipped by the leading-quote match). Two
# call shapes: ctx.hook("tag", ...) and hook_if_set(ctx, "tag", ...).
TAGS="$(cat ${TEMPLATE_FILES} \
        | grep -oE '(\.hook\(|hook_if_set\(ctx, ?)"[a-z_0-9]+"' \
        | sed 's/.*"\(.*\)"/\1/' | sort -u)"

if [ -z "${TAGS}" ]; then
    echo "check_hook_tags: no hook tags found in ${TEMPLATE_FILES}" >&2
    exit 1
fi

DEAD=""
while IFS= read -r tag; do
    [ -n "${tag}" ] || continue
    if ! grep -rqF "${tag}" "${CORE_SRC}" \
            --include='*.cpp' --include='*.h' --include='*.hpp'; then
        DEAD="${DEAD}  ${tag}
"
    fi
done <<EOF
${TAGS}
EOF

if [ -n "${DEAD}" ]; then
    echo "check_hook_tags: FAIL - hook tags Core does not read:" >&2
    printf '%s' "${DEAD}" >&2
    echo "These tags produce dead hooks. Remove them from the template," >&2
    echo "or add a Core reader first and cite it in the tag's comment." >&2
    exit 1
fi

echo "check_hook_tags: OK - all template hook tags are read by Core"
