#!/usr/bin/env bash
# Manager role: motivate incomplete external contracts toward NexusCore.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FILTER="${GROKIUM_SMX_FILTER_BIN:-$ROOT/build/grokium-smx-filter}"
DIR="${1:-${GROKIUM_CONTRACT_DIR:-$ROOT/data/contracts}}"
export GROKIUM_CONTRACT_DIR="$DIR"

if [[ ! -x "$FILTER" ]]; then
  make -C "$ROOT/c_core" all
fi

"$FILTER" manager-tick "$DIR"

CUBALC="$ROOT/deps/cubalc/out/cubalc"
if [[ -x "$CUBALC" ]]; then
  "$CUBALC" run "$ROOT/cubalc/programs/hive/manager_motivate.cubalc" || true
fi
