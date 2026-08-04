#!/usr/bin/env bash
# Form external-nanobot contract with accept criteria (CubalC + filter CLI).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FILTER="${GROKIUM_SMX_FILTER_BIN:-$ROOT/build/grokium-smx-filter}"
export GROKIUM_CONTRACT_DIR="${GROKIUM_CONTRACT_DIR:-$ROOT/data/contracts}"
mkdir -p "$GROKIUM_CONTRACT_DIR"

ASSIGNEE=""
TASK=""
DIGIT="-1"
MIN_SET="0"
SMX_SHA=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --assignee) ASSIGNEE="$2"; shift 2 ;;
    --task) TASK="$2"; shift 2 ;;
    --digit) DIGIT="$2"; shift 2 ;;
    --min-set) MIN_SET="$2"; shift 2 ;;
    --smx-sha) SMX_SHA="$2"; shift 2 ;;
    *) echo "unknown $1" >&2; exit 2 ;;
  esac
done

if [[ -z "$ASSIGNEE" || -z "$TASK" ]]; then
  echo "usage: $0 --assignee ID --task TEXT [--digit N] [--min-set N] [--smx-sha HEX]" >&2
  exit 2
fi

if [[ ! -x "$FILTER" ]]; then
  make -C "$ROOT/c_core" all
fi

ARGS=(form --assignee "$ASSIGNEE" --task "$TASK" --min-set "$MIN_SET")
if [[ "$DIGIT" != "-1" ]]; then ARGS+=(--digit "$DIGIT"); fi
if [[ -n "$SMX_SHA" ]]; then ARGS+=(--smx-sha "$SMX_SHA"); fi

"$FILTER" "${ARGS[@]}"
# Optional CubalC board smoke (accept template)
CUBALC="$ROOT/deps/cubalc/out/cubalc"
if [[ -x "$CUBALC" ]]; then
  "$CUBALC" run "$ROOT/cubalc/programs/hive/external_contract.cubalc" >/dev/null 2>&1 || true
fi
