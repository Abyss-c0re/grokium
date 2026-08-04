#!/usr/bin/env bash
# Validate external contract result (CubalC board + C filter + optional smart contract).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FILTER="${GROKIUM_SMX_FILTER_BIN:-$ROOT/build/grokium-smx-filter}"
PATH_JSON="${1:-}"
BITS="${2:-}"

if [[ -z "$PATH_JSON" ]]; then
  echo "usage: $0 data/contracts/<id>.json [01-bits or bits-file]" >&2
  exit 2
fi
if [[ ! -x "$FILTER" ]]; then
  make -C "$ROOT/c_core" all
fi

ARGS=(validate "$PATH_JSON")
if [[ -n "$BITS" ]]; then
  if [[ -f "$BITS" ]]; then
    ARGS+=(--bits-file "$BITS")
  else
    ARGS+=(--bits "$BITS")
  fi
fi

# CubalC accept board (machine ASSERTs)
CUBALC="$ROOT/deps/cubalc/out/cubalc"
if [[ -x "$CUBALC" ]]; then
  if ! "$CUBALC" run "$ROOT/cubalc/programs/hive/external_contract.cubalc"; then
    echo '{"cubalc_accept":false}' >&2
    # still try C criteria
  fi
fi

"$FILTER" "${ARGS[@]}"
