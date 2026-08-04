#!/usr/bin/env bash
# Pull/build CubalC from public repo into deps/cubalc
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/deps/cubalc"
REPO="${CUBALC_REPO:-https://github.com/Abyss-c0re/cubalc.git}"
REF="${CUBALC_REF:-main}"

mkdir -p "$ROOT/deps"
if [[ -d "$DEST/.git" ]]; then
  git -C "$DEST" fetch origin
  git -C "$DEST" checkout "$REF"
  git -C "$DEST" pull --ff-only origin "$REF" || true
else
  git clone --branch "$REF" "$REPO" "$DEST"
fi
make -C "$DEST" all
echo "CubalC synced: $DEST"
echo "  bin: $DEST/out/cubalc"
"$DEST/out/cubalc" version 2>/dev/null || "$DEST/out/cubalc" help 2>&1 | head -3
