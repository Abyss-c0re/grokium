#!/usr/bin/env bash
# Sync nanobot agent core into deps/nanobot (submodule or sibling).
# Prefer git submodule; fall back to sibling checkout for local dev.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${GROKIUM_NANOBOT_DEST:-$ROOT/deps/nanobot}"
PIN_FILE="$ROOT/deps/NANOBOT_PIN.txt"
REMOTE="${NANOBOT_REMOTE:-https://github.com/Abyss-c0re/nanobot.git}"
SIBLING="${NANOBOT_SIBLING:-$HOME/Dev/AI/nanobot}"

mkdir -p "$(dirname "$DEST")" "$ROOT/deps"

if [[ -f "$ROOT/.gitmodules" ]] && grep -q 'deps/nanobot' "$ROOT/.gitmodules" 2>/dev/null; then
  git -C "$ROOT" submodule update --init --recursive deps/nanobot 2>/dev/null || true
fi

if [[ ! -f "$DEST/CMakeLists.txt" ]]; then
  if [[ -f "$SIBLING/CMakeLists.txt" && "${NANOBOT_USE_SIBLING:-1}" == "1" ]]; then
    ln -sfn "$SIBLING" "$DEST"
    echo "sync_nanobot: linked $DEST -> $SIBLING"
  else
    git clone --recurse-submodules "$REMOTE" "$DEST"
  fi
fi

# Ensure braincube submodule inside nanobot
if [[ -f "$DEST/.gitmodules" ]]; then
  git -C "$DEST" submodule update --init --recursive third_party/braincube 2>/dev/null || true
fi

if [[ -d "$DEST/.git" || -f "$DEST/.git" ]]; then
  rev="$(git -C "$DEST" rev-parse HEAD 2>/dev/null || echo unknown)"
  ver="$(cat "$DEST/VERSION" 2>/dev/null || echo '?')"
else
  rev="symlink-or-copy"
  ver="$(cat "$DEST/VERSION" 2>/dev/null || echo '?')"
fi

{
  echo "# nanobot pin — embeddable C agent core (ours). Not xAI."
  echo "repo=https://github.com/Abyss-c0re/nanobot"
  echo "commit=$rev"
  echo "version=$ver"
  echo "path=deps/nanobot"
  echo "braincells=1"
  echo "braincube=third_party/braincube"
  echo "host=C"
  echo "python=0"
  echo "embed=armv7,static,lean"
} > "$PIN_FILE"

echo "sync_nanobot: $DEST @ $rev (v$ver)"
