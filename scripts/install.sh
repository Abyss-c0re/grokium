#!/usr/bin/env bash
# Grokium installer — local-first zero-telemetry harness (Apache-2.0).
# Not affiliated with xAI / Grok.
#
# One line:
#   curl -fsSL https://raw.githubusercontent.com/Abyss-c0re/grokium/main/scripts/install.sh | bash
#
# Options:
#   curl -fsSL …/install.sh | bash -s -- --user
#   curl -fsSL …/install.sh | bash -s -- --with-nanobot
#   curl -fsSL …/install.sh | bash -s -- --prefix ~/.local --no-nanobot
#   curl -fsSL …/install.sh | bash -s -- --update
#   curl -fsSL …/install.sh | bash -s -- --uninstall
set -euo pipefail

if [ -z "${BASH_VERSION:-}" ]; then
  command -v bash >/dev/null 2>&1 && exec bash "$0" "$@"
fi

REPO_URL="${REPO_URL:-https://github.com/Abyss-c0re/grokium.git}"
RAW_BASE="${RAW_BASE:-https://raw.githubusercontent.com/Abyss-c0re/grokium}"
CHANNEL="${CHANNEL:-main}"
PREFIX="${PREFIX:-}"
INSTALL_MODE="${INSTALL_MODE:-user}"
ACTION="${ACTION:-}"
WITH_NANOBOT="${WITH_NANOBOT:-auto}"  # auto | 1 | 0
NO_PROMPT="${NO_PROMPT:-0}"
WORKDIR="${TMPDIR:-/tmp}/grokium-install-$$"

usage() {
  cat <<'U'
Grokium install — local agent harness (Apache-2.0)

  curl -fsSL https://raw.githubusercontent.com/Abyss-c0re/grokium/main/scripts/install.sh | bash

  --user              install under ~/.local + clone under ~/.local/src/grokium (default)
  --prefix DIR        binary/link prefix (default: ~/.local)
  --src DIR           clone/checkout directory (default: $PREFIX/src/grokium)
  --channel REF       git ref (default: main)
  --with-nanobot      also install nanobot peer binary (curl nanobot install.sh --user)
  --no-nanobot        skip nanobot
  --update            git pull / re-link (keep data/)
  --uninstall         remove launcher links (keeps clone unless --wipe)
  --wipe              with --uninstall: remove clone dir
  --no-prompt, -y     non-interactive
  -h, --help

After install:
  grokium --version
  grokium status
  grokium serve          # API :17444
  grokium nanobot status # fleet (needs nanobot on PATH)

Nanobot is a separate repo: https://github.com/Abyss-c0re/nanobot
U
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage ;;
    --user|--local) INSTALL_MODE=user; shift ;;
    --prefix) PREFIX="$2"; shift 2 ;;
    --src) SRC_DIR="$2"; shift 2 ;;
    --channel) CHANNEL="$2"; shift 2 ;;
    --with-nanobot) WITH_NANOBOT=1; shift ;;
    --no-nanobot) WITH_NANOBOT=0; shift ;;
    --update) ACTION=update; shift ;;
    --uninstall|--remove) ACTION=uninstall; shift ;;
    --wipe) WIPE=1; shift ;;
    --no-prompt|-y|--yes) NO_PROMPT=1; shift ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

log() { printf 'grokium-install: %s\n' "$*" >&2; }
die() { log "ERROR: $*"; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

need_python() {
  have python3 || die "need python3 on PATH (3.11+)"
  local v
  v=$(python3 -c 'import sys; print("%d.%d"%sys.version_info[:2])' 2>/dev/null || echo 0)
  python3 -c 'import sys; raise SystemExit(0 if sys.version_info>=(3,11) else 1)' \
    || die "need Python >= 3.11 (found $v)"
}

operator_home() {
  if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != root ]]; then
    eval echo "~$SUDO_USER" 2>/dev/null || echo "${HOME}"
  else
    echo "${HOME}"
  fi
}

OHOME=$(operator_home)
PREFIX="${PREFIX:-$OHOME/.local}"
SRC_DIR="${SRC_DIR:-$PREFIX/src/grokium}"
BIN_DIR="$PREFIX/bin"
REGISTRY="$OHOME/.grokium/install.env"

write_registry() {
  mkdir -p "$(dirname "$REGISTRY")"
  cat >"$REGISTRY" <<E
REPO_URL=$REPO_URL
CHANNEL=$CHANNEL
PREFIX=$PREFIX
SRC_DIR=$SRC_DIR
BIN_DIR=$BIN_DIR
INSTALLED_AT=$(date -Iseconds 2>/dev/null || date)
E
}

link_launchers() {
  mkdir -p "$BIN_DIR"
  # thin wrappers so PATH works without PYTHONPATH
  cat >"$BIN_DIR/grokium" <<E
#!/usr/bin/env bash
export PYTHONPATH="${SRC_DIR}/src\${PYTHONPATH:+:\$PYTHONPATH}"
export GROKIUM_CONFIG="\${GROKIUM_CONFIG:-${SRC_DIR}/config/grokium.toml}"
exec python3 -m grokium.cli "\$@"
E
  cat >"$BIN_DIR/grokium-mcp" <<E
#!/usr/bin/env bash
export PYTHONPATH="${SRC_DIR}/src\${PYTHONPATH:+:\$PYTHONPATH}"
export GROKIUM_CONFIG="\${GROKIUM_CONFIG:-${SRC_DIR}/config/grokium.toml}"
exec python3 -m grokium.cli mcp "\$@"
E
  chmod 755 "$BIN_DIR/grokium" "$BIN_DIR/grokium-mcp"
}

path_hint() {
  case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *)
      log "add to PATH: export PATH=\"$BIN_DIR:\$PATH\""
      if [[ -f "$OHOME/.bashrc" ]] && ! grep -qF "$BIN_DIR" "$OHOME/.bashrc" 2>/dev/null; then
        if [[ "$NO_PROMPT" == "1" ]] || [[ ! -t 0 ]]; then
          :
        else
          log "tip: echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> ~/.bashrc"
        fi
      fi
      ;;
  esac
}

install_nanobot() {
  log "installing nanobot peer (separate repo)…"
  if have nanobot; then
    log "nanobot already on PATH: $(command -v nanobot) ($(nanobot --version 2>/dev/null | head -1))"
    return 0
  fi
  local nb_install
  nb_install=$(curl -fsSL "https://raw.githubusercontent.com/Abyss-c0re/nanobot/main/scripts/install.sh" || true)
  if [[ -z "$nb_install" ]]; then
    log "WARN: could not fetch nanobot install.sh — install later from https://github.com/Abyss-c0re/nanobot"
    return 0
  fi
  bash -s -- --user --no-prompt --skip-start <<<"$nb_install" \
    || curl -fsSL "https://raw.githubusercontent.com/Abyss-c0re/nanobot/main/scripts/install.sh" \
         | bash -s -- --user --no-prompt --skip-start \
    || log "WARN: nanobot install failed — fleet deploy needs: ~/.local/bin/nanobot"
}

clone_or_update() {
  have git || die "need git"
  if [[ -d "$SRC_DIR/.git" ]]; then
    log "updating $SRC_DIR ($CHANNEL)"
    git -C "$SRC_DIR" fetch --depth 1 origin "$CHANNEL" 2>/dev/null \
      || git -C "$SRC_DIR" fetch origin "$CHANNEL"
    git -C "$SRC_DIR" checkout -q "$CHANNEL" 2>/dev/null \
      || git -C "$SRC_DIR" checkout -q -B "$CHANNEL" "origin/$CHANNEL" 2>/dev/null \
      || git -C "$SRC_DIR" pull --ff-only
  else
    log "cloning $REPO_URL → $SRC_DIR"
    mkdir -p "$(dirname "$SRC_DIR")"
    git clone --depth 1 --branch "$CHANNEL" "$REPO_URL" "$SRC_DIR" 2>/dev/null \
      || git clone --depth 1 "$REPO_URL" "$SRC_DIR"
    if [[ "$CHANNEL" != "main" && "$CHANNEL" != "master" ]]; then
      git -C "$SRC_DIR" fetch --depth 1 origin "$CHANNEL" 2>/dev/null || true
      git -C "$SRC_DIR" checkout "$CHANNEL" 2>/dev/null || true
    fi
  fi
}

do_install() {
  need_python
  clone_or_update
  # optional editable pip (best-effort; wrappers work without)
  if have pip3 || python3 -m pip --version >/dev/null 2>&1; then
    log "pip install -e (user)…"
    python3 -m pip install --user -e "$SRC_DIR" >/dev/null 2>&1 \
      || log "pip -e skipped (wrappers still work)"
  fi
  link_launchers
  write_registry
  path_hint
  case "$WITH_NANOBOT" in
    1|yes|true) install_nanobot ;;
    0|no|false) log "skipping nanobot (--no-nanobot)" ;;
    auto)
      if have nanobot; then
        log "nanobot already present"
      else
        install_nanobot
      fi
      ;;
  esac
  log "OK grokium → $BIN_DIR/grokium"
  log "source tree → $SRC_DIR"
  if "$BIN_DIR/grokium" --version 2>/dev/null; then
    :
  else
    PATH="$BIN_DIR:$PATH" grokium --version 2>/dev/null || true
  fi
  cat <<E >&2

  try:
    export PATH="$BIN_DIR:\$PATH"
    grokium --version
    grokium status
    grokium serve          # http://127.0.0.1:17444
    grokium nanobot status # after nanobot is on PATH

  fleet:  https://github.com/Abyss-c0re/nanobot
  pin:    $SRC_DIR/NANOBOT_PIN.txt
E
}

do_uninstall() {
  rm -f "$BIN_DIR/grokium" "$BIN_DIR/grokium-mcp"
  log "removed launchers from $BIN_DIR"
  if [[ "${WIPE:-0}" == "1" ]]; then
    rm -rf "$SRC_DIR"
    log "removed $SRC_DIR"
  else
    log "clone kept at $SRC_DIR (pass --wipe to delete)"
  fi
  rm -f "$REGISTRY"
}

# load prior install if present
if [[ -f "$REGISTRY" ]]; then
  # shellcheck disable=SC1090
  set -a; . "$REGISTRY" 2>/dev/null || true; set +a
  PREFIX="${PREFIX:-$OHOME/.local}"
  SRC_DIR="${SRC_DIR:-$PREFIX/src/grokium}"
  BIN_DIR="${BIN_DIR:-$PREFIX/bin}"
fi

ACTION="${ACTION:-install}"
case "$ACTION" in
  update) ACTION=install ;; # same path
  uninstall) do_uninstall ;;
  install) do_install ;;
  *) die "unknown action $ACTION" ;;
esac
