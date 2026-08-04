#!/usr/bin/env bash
# Core image Linux groups/users — layer separation for Hive Mind.
# Law: .agents/laws/06-HIVE_MIND_CORE.md
# External nanobots are NEVER members of hive-core.
set -euo pipefail

MODE="${1:-help}"

GROUPS=(hive-core hive-filter hive-nb hive-external)
USERS_CORE=(grokium-core)
USERS_FILTER=(grokium-filter)
USERS_NB=(nb-manager)
USERS_EXT=(nb-worker)

install_groups() {
  for g in "${GROUPS[@]}"; do
    if getent group "$g" >/dev/null 2>&1; then
      echo "group ok: $g"
    else
      groupadd --system "$g"
      echo "group created: $g"
    fi
  done
}

ensure_user() {
  local u="$1" g="$2" home="$3"
  if id "$u" >/dev/null 2>&1; then
    usermod -a -G "$g" "$u" 2>/dev/null || true
    echo "user ok: $u ($g)"
  else
    useradd --system --create-home --home-dir "$home" \
      --shell /usr/sbin/nologin --gid "$g" "$u"
    echo "user created: $u home=$home gid=$g"
  fi
}

install_users() {
  install_groups
  ensure_user grokium-core   hive-core     /var/lib/grokium/core
  ensure_user grokium-filter hive-filter   /var/lib/grokium/filter
  ensure_user nb-manager     hive-nb       /var/lib/nanobot/nb-manager
  ensure_user nb-worker      hive-external /var/lib/nanobot/nb-worker
  # core must not sit in hive-external
  if id grokium-core >/dev/null 2>&1; then
    gpasswd -d grokium-core hive-external 2>/dev/null || true
  fi
  echo "ACL: core secrets mode 0750 under /var/lib/grokium/core"
  mkdir -p /var/lib/grokium/core /var/lib/grokium/filter \
           /var/lib/nanobot/nb-manager /var/lib/nanobot/nb-worker
  chown grokium-core:hive-core /var/lib/grokium/core
  chown grokium-filter:hive-filter /var/lib/grokium/filter
  chown nb-manager:hive-nb /var/lib/nanobot/nb-manager
  chown nb-worker:hive-external /var/lib/nanobot/nb-worker
  chmod 0750 /var/lib/grokium/core /var/lib/grokium/filter
  chmod 0750 /var/lib/nanobot/nb-manager /var/lib/nanobot/nb-worker
  cat <<'EOF'
NEXUS_COORD v1 | from=core_image | type=users_install | status=ONLINE |
HOLD_FLASH=ack_held | groups=hive-core,hive-filter,hive-nb,hive-external |
external≠core | observer=NexusCore |
EOF
}

print_manifest() {
  cat <<'EOF'
# Hive Mind core image identity plate (no secrets)

| UID | GID | Layer | Wire |
|-----|-----|-------|------|
| grokium-core | hive-core | BrainCube mini-hive + SoT | internal |
| grokium-filter | hive-filter | SMX filter + contracts | SMX2 |
| nb-manager | hive-nb | motivate incomplete | SMX motivate |
| nb-worker* | hive-external | external cells | SMX2 only |

Instinct: core MatrixState is queen; cells are bees.
All Hail NexusCore.
EOF
}

case "$MODE" in
  install)
    if [[ "$(id -u)" -ne 0 ]]; then
      echo "install needs root (sudo $0 install)" >&2
      exit 1
    fi
    install_users
    ;;
  manifest|print)
    print_manifest
    ;;
  dry-run)
    print_manifest
    echo "(dry-run: no useradd)"
    for g in "${GROUPS[@]}"; do echo "would ensure group $g"; done
    ;;
  *)
    echo "usage: $0 install|manifest|dry-run"
    exit 2
    ;;
esac
