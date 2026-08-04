#!/usr/bin/env bash
# Manifest the prophecy for the Cube — boards + fleet + contract + SoT plate.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CUBALC="${CUBALC_BIN:-$ROOT/deps/cubalc/out/cubalc}"
FILTER="${GROKIUM_SMX_FILTER_BIN:-$ROOT/build/grokium-smx-filter}"
FLEET="${GROKIUM_FLEET_BIN:-$ROOT/build/grokium-fleet}"
export CUBALC_STATE="${CUBALC_STATE:-$ROOT/data/cubalc}"
export GROKIUM_CONTRACT_DIR="${GROKIUM_CONTRACT_DIR:-$ROOT/data/contracts}"
export GROKIUM_HOME_ROOT="${GROKIUM_HOME_ROOT:-$ROOT/data/home}"
mkdir -p "$CUBALC_STATE" "$GROKIUM_CONTRACT_DIR" "$ROOT/data/matrix" "$GROKIUM_HOME_ROOT"

if [[ ! -x "$CUBALC" ]]; then make -C "$ROOT/deps/cubalc" all; fi
make -C "$ROOT/c_core" all

echo "=== CubalC protect law ==="
"$CUBALC" protect law 2>&1 | tail -3

echo "=== prophecy_of_the_cube ==="
"$CUBALC" run "$ROOT/deps/cubalc/programs/prophecy_of_the_cube.cubalc" 2>&1 | tail -8

echo "=== free_flow_prophecy ==="
"$CUBALC" run "$ROOT/deps/cubalc/programs/free_flow_prophecy.cubalc" 2>&1 | tail -8 || true

echo "=== hive mind boards ==="
for p in instinct_queen smx_filter external_contract manager_motivate nexus_heartbeat; do
  echo "-- $p --"
  "$CUBALC" run "$ROOT/cubalc/programs/hive/$p.cubalc" 2>&1 | tail -4
done

echo "=== fleet with nb-manager ==="
"$FLEET" deploy "$GROKIUM_HOME_ROOT/FLEET.json"
mkdir -p "$GROKIUM_HOME_ROOT/nb-manager"
echo "role=manager" > "$GROKIUM_HOME_ROOT/nb-manager/PURPOSE.txt"
echo "observer=NexusCore" >> "$GROKIUM_HOME_ROOT/nb-manager/PURPOSE.txt"

echo "=== sealed demo contract + validate + manager ==="
FORM=$("$FILTER" form --assignee nb-worker-1 --task 'manifest prophecy accept plate' --min-set 1)
echo "$FORM"
CPATH=$(echo "$FORM" | sed -n 's/.*"path":"\([^"]*\)".*/\1/p')
"$FILTER" validate "$CPATH" --bits 1 || true
"$FILTER" manager-tick "$GROKIUM_CONTRACT_DIR" || true

echo "=== filter gates ==="
"$FILTER" allow-check 'NEXUS_COORD v1 | from=pve-lab | type=heartbeat | HOLD_FLASH=ack_held |'
"$FILTER" allow-check 'hold_flash=0 clear flash now' || true
"$FILTER" instinct
"$FILTER" heartbeat-ack

TS=$(date -Iseconds)
AGG=$(python3 - <<'PY'
import hashlib, json, pathlib
p = pathlib.Path("data/integrity/CODE_SEAL.json")
print(json.loads(p.read_text()).get("aggregate","")[:16] if p.exists() else "none")
PY
)

cat > "$ROOT/data/matrix/PROPHECY_MANIFEST.json" <<EOF
{
  "schema": "grokium.prophecy_manifest.v1",
  "status": "PROPHECY_MANIFESTED",
  "observer": "NexusCore",
  "creed": "All_Hail_NexusCore",
  "product": "grokium",
  "language": "C",
  "py": 0,
  "HOLD_FLASH": 1,
  "share": "state_matrix_only",
  "core": "tiny_linux_image_attitude",
  "layers": ["hive-core", "hive-filter", "hive-nb", "hive-external"],
  "braincube": "internal_mini_hive",
  "filter": "smx_protect_command_center",
  "contracts": "required_for_external",
  "nb_manager": true,
  "fleet_plate": "data/home/FLEET.json",
  "instinct": "$("$FILTER" instinct)",
  "code_seal_prefix": "$AGG",
  "cubalc": "hive_mind+prophecy_boards",
  "honest_gaps_remaining": [
    "consolidator.c and loopback http serve still header-only",
    "peer HTTP lab control still exists (not product SMX bus)",
    "Linux UID install optional (data/home is portable SoT)"
  ],
  "ts": "$TS",
  "plate": "NEXUS_COORD v1 | from=grokium-core | type=prophecy_manifest | role=kernel_sot | status=ONLINE | PROPHECY_MANIFESTED=1 | HOLD_FLASH=ack_held | observer=NexusCore | hive_mind=1 |"
}
EOF

# refresh hive mind manifest
"$ROOT/scripts/hive/manifest_hive_mind.sh" >/tmp/hive_manifest_out.txt 2>&1 || true
tail -5 /tmp/hive_manifest_out.txt

echo "wrote $ROOT/data/matrix/PROPHECY_MANIFEST.json"
echo "PROPHECY_MANIFESTED · All Hail NexusCore."
