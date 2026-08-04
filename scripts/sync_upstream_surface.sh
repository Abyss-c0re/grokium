#!/usr/bin/env bash
# Capture local surface of an *installed* official grok CLI (if any).
# Does NOT download, vendor, or redistribute xAI binaries or source.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/data/upstream_surface"
mkdir -p "$OUT"

GROK_BIN="${GROK_CLI:-$(command -v grok 2>/dev/null || true)}"
{
  echo "# generated $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# purpose: local compat surface only — not a redistributed product"
  echo "grok_bin=${GROK_BIN:-}"
} >"$OUT/meta.txt"

if [[ -z "$GROK_BIN" || ! -x "$GROK_BIN" ]]; then
  echo "no_official_cli=1" >>"$OUT/meta.txt"
  echo "sync_upstream_surface: no grok on PATH (ok)"
  exit 0
fi

"$GROK_BIN" --version >"$OUT/version.txt" 2>&1 || true
"$GROK_BIN" --help >"$OUT/help.txt" 2>&1 || true
if [[ -f "${HOME}/.grok/.metadata_version" ]]; then
  cp -f "${HOME}/.grok/.metadata_version" "$OUT/metadata_version" || true
fi
if [[ -f "${HOME}/.grok/CHANGELOG.md" ]]; then
  head -n 40 "${HOME}/.grok/CHANGELOG.md" >"$OUT/changelog_head.md" || true
fi

ver="$(tr -d '\r' <"$OUT/version.txt" | head -1 | sed -n 's/.*\([0-9]\+\.[0-9]\+\.[0-9]\+\).*/\1/p')"
[[ -z "$ver" ]] && ver="unknown"
ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
printf '%s\n' \
  '{' \
  '  "schema": "grokium.upstream_surface.v1",' \
  "  \"official_cli_version\": \"$ver\"," \
  "  \"captured_at\": \"$ts\"," \
  '  "source": "local_installed_cli",' \
  '  "redistributes_binary": false' \
  '}' >"$OUT/compat_snippet.json"

COMPAT="$ROOT/data/grok_build_compat.json"
mkdir -p "$ROOT/data"
printf '%s\n' \
  '{' \
  '  "schema": "grokium.version_compat.v1",' \
  "  \"reported_grok_build_version\": \"$ver\"," \
  '  "grokium_version": "0.4.0-nanobot",' \
  '  "last_source": "sync_upstream_surface",' \
  "  \"updated_at\": $(date +%s)" \
  '}' >"$COMPAT"

echo "sync_upstream_surface: official=$ver -> $OUT"
