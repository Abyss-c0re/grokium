#!/usr/bin/env bash
# Sync braincube decision core into deps/braincube (submodule or sibling).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${GROKIUM_BRAINCUBE_DEST:-$ROOT/deps/braincube}"
PIN_FILE="$ROOT/deps/BRAINCUBE_PIN.txt"
REMOTE="${BRAINCUBE_REMOTE:-https://github.com/Abyss-c0re/braincube.git}"
SIBLING="${BRAINCUBE_SIBLING:-$HOME/Dev/AI/braincube}"

mkdir -p "$(dirname "$DEST")" "$ROOT/deps"

if [[ -f "$ROOT/.gitmodules" ]] && grep -q 'deps/braincube' "$ROOT/.gitmodules" 2>/dev/null; then
  git -C "$ROOT" submodule update --init --recursive deps/braincube 2>/dev/null || true
fi

if [[ ! -f "$DEST/CMakeLists.txt" && ! -f "$DEST/include/braincube/braincube.h" ]]; then
  if [[ -f "$SIBLING/CMakeLists.txt" || -f "$SIBLING/include/braincube/braincube.h" ]] && \
     [[ "${BRAINCUBE_USE_SIBLING:-1}" == "1" ]]; then
    ln -sfn "$SIBLING" "$DEST"
    echo "sync_braincube: linked $DEST -> $SIBLING"
  else
    git clone "$REMOTE" "$DEST"
  fi
fi

if [[ -d "$DEST/.git" || -f "$DEST/.git" ]]; then
  rev="$(git -C "$DEST" rev-parse HEAD 2>/dev/null || echo unknown)"
else
  rev="symlink-or-copy"
fi

{
  echo "# braincube pin — LHLAM decision core (research). Not xAI."
  echo "repo=https://github.com/Abyss-c0re/braincube"
  echo "commit=$rev"
  echo "path=deps/braincube"
  echo "role=decision-core"
  echo "cells=nanobot-braincells"
} > "$PIN_FILE"

echo "sync_braincube: $DEST @ $rev"
