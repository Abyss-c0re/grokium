#!/usr/bin/env bash
# Functional smoke — nanobot-linked host (+ optional CubalC board)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export GROKIUM_ROOT="$ROOT"
export CUBALC_STATE="${CUBALC_STATE:-$ROOT/data/cubalc}"
export CUBALC_BIN="${CUBALC_BIN:-$ROOT/deps/cubalc/out/cubalc}"
BIN="$ROOT/scripts/grokium"
fail=0
pass=0
check() {
  local name="$1" want="$2"
  shift 2
  set +e
  out=$("$@" 2>&1)
  rc=$?
  set -e
  if [[ "$rc" -eq "$want" ]]; then
    echo "PASS $name (rc=$rc)"
    pass=$((pass+1))
  else
    echo "FAIL $name (rc=$rc want=$want)"
    echo "$out" | tail -12
    fail=$((fail+1))
  fi
}

bash "$ROOT/scripts/sync_nanobot.sh" >/dev/null
make -C "$ROOT/host" all >/dev/null

check version 0 "$BIN" version
check help 0 "$BIN" help
check compat 0 "$BIN" compat

if [[ -x "$CUBALC_BIN" ]]; then
  check selftest 0 "$BIN" selftest
  check board 0 "$BIN" board
else
  echo "SKIP cubalc board (no cubalc binary)"
fi

# local models + chat if llama :1212 up
if curl -sS -m 2 -o /dev/null http://127.0.0.1:1212/v1/models 2>/dev/null; then
  check models 0 "$BIN" models
  check chat_p 0 "$BIN" -p "Say only the word: OK"
else
  echo "SKIP models/chat (llama :1212 down)"
fi

echo "RESULT pass=$pass fail=$fail"
[[ "$fail" -eq 0 ]]
