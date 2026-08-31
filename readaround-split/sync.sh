#!/bin/bash
# Copies this directory's tracked kernel source files into a full,
# buildable kernel tree so they can be compiled. See README.md for the
# full build -> boot -> observe cycle this is one step of.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="${1:-$HOME/linux-7.1.2}"

if [ ! -d "$DEST" ]; then
    echo "error: destination kernel tree not found: $DEST" >&2
    echo "usage: $0 [path-to-kernel-tree]" >&2
    exit 1
fi

for f in "$SCRIPT_DIR"/mm/*.c "$SCRIPT_DIR"/block/*.c; do
    rel="${f#"$SCRIPT_DIR"/}"
    target="$DEST/$rel"
    if [ ! -f "$target" ]; then
        echo "warn: $rel not found in $DEST, skipping" >&2
        continue
    fi
    if cmp -s "$f" "$target"; then
        echo "unchanged $rel"
    else
        cp "$f" "$target"
        echo "synced    $rel"
    fi
done
