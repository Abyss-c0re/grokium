#!/usr/bin/env bash
# Manifest Hive Mind core: CubalC boards + filter instinct + heartbeat fold.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CUBALC="$ROOT/deps/cubalc/out/cubalc"
FILTER="${GROKIUM_SMX_FILTER_BIN:-$ROOT/build/grokium-smx-filter}"
STATE="$ROOT/data/matrix"
mkdir -p "$STATE" "$ROOT/data/contracts"

if [[ ! -x "$CUBALC" ]]; then
  make -C "$ROOT/deps/cubalc" all
fi
if [[ ! -x "$FILTER" ]]; then
  make -C "$ROOT/c_core" all
fi

echo "=== instinct (queen) ==="
"$CUBALC" run "$ROOT/cubalc/programs/hive/instinct_queen.cubalc"
echo "=== smx_filter ==="
"$CUBALC" run "$ROOT/cubalc/programs/hive/smx_filter.cubalc"
echo "=== external_contract ==="
"$CUBALC" run "$ROOT/cubalc/programs/hive/external_contract.cubalc"
echo "=== manager_motivate ==="
"$CUBALC" run "$ROOT/cubalc/programs/hive/manager_motivate.cubalc"
echo "=== nexus_heartbeat ==="
"$CUBALC" run "$ROOT/cubalc/programs/hive/nexus_heartbeat.cubalc"
echo "=== protect (upstream) ==="
"$CUBALC" protect law 2>/dev/null | head -3 || true

"$FILTER" instinct
"$FILTER" heartbeat-ack

INSTINCT=$("$FILTER" instinct)
cat > "$STATE/HIVE_MIND_MANIFEST.json" <<EOF
{
  "schema": "grokium.hive_mind.v1",
  "observer": "NexusCore",
  "core": "tiny_linux_image",
  "layers": ["hive-core", "hive-filter", "hive-nb", "hive-external"],
  "wire": "smx2",
  "product_wire": "smx2",
  "peer_http": "lab_ops_only",
  "peer_http_is_product_bus": false,
  "llm_is_commander": false,
  "hold_flash": 1,
  "HOLD_FLASH": 1,
  "share": "state_matrix_only",
  "instinct": "$INSTINCT",
  "ts": "$(date -Iseconds)"
}
EOF
grep -q '"product_wire": "smx2"' "$STATE/HIVE_MIND_MANIFEST.json"
grep -q '"peer_http_is_product_bus": false' "$STATE/HIVE_MIND_MANIFEST.json"
echo "wrote $STATE/HIVE_MIND_MANIFEST.json"
echo "All Hail NexusCore."
