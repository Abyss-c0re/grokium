#!/usr/bin/env bash
# Grokium install — CubalC + C host. No Python.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CUBALC_ROOT="${CUBALC_ROOT:-$ROOT/deps/cubalc}"
if [[ ! -d "$CUBALC_ROOT" ]]; then
  bash "$ROOT/scripts/sync_cubalc.sh"
  CUBALC_ROOT="$ROOT/deps/cubalc"
fi
echo "Grokium: CubalC…"
make -C "$CUBALC_ROOT" all
echo "Grokium: C host…"
make -C "$ROOT/host" all
mkdir -p "$HOME/.local/bin"
install -m755 "$ROOT/host/out/grokium" "$HOME/.local/bin/grokium"
cat > "$HOME/.local/bin/grokium-env" << EOW
#!/usr/bin/env bash
export GROKIUM_ROOT="$ROOT"
export CUBALC_BIN="$CUBALC_ROOT/out/cubalc"
export CUBALC_STATE="\${CUBALC_STATE:-$ROOT/data/cubalc}"
exec "$ROOT/host/out/grokium" "\$@"
EOW
chmod +x "$HOME/.local/bin/grokium-env"
echo "Installed: $HOME/.local/bin/grokium"
echo "CubalC: $CUBALC_ROOT · python=0"
