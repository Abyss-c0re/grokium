# Nanobot fleet map

Primary fleet plate is **pure C** (`c_core`): purpose-assigned roles including
`nb-manager`, honest `pid` / `status` / `offline` / `separated`, product wire
**smx2**. Peer HTTP on bot ports is **lab_ops_only** — never the product bus.

## CLI (`grokium-fleet` / `./scripts/grokium fleet`)

```bash
make -C c_core all
./scripts/grokium fleet              # defaults (roles)
./scripts/grokium fleet deploy       # mkdir homes; clear live pids on plate
./scripts/grokium fleet status       # load + kill(0) probe + rewrite honest plate
./scripts/grokium fleet spawn ID     # fork/exec NANOBOT_BIN (lab peer port)
./scripts/grokium fleet spawn-all
./scripts/grokium fleet note-pid ID PID   # preserves other bots' pids
./scripts/grokium fleet separate ID  # SIGTERM if live, mark separated
./scripts/grokium fleet stop-all
./scripts/grokium fleet selftest     # pure-C pid/status dual-wire honesty
./scripts/grokium fleet cubalc       # optional CubalC board path
```

Env: `NANOBOT_BIN` (default `nanobot`), `GROKIUM_HOME_ROOT` (default `data/home`).
Spawn writes `PURPOSE.txt` + `nanobot.log` under each home.

### Honesty rules

| Rule | Behavior |
|------|----------|
| `status` | `kill(0)` probe; dead pids cleared; plate rewritten |
| `note-pid` | live pid recorded; dead pid rejected/cleared immediately |
| `save` / plate | always re-probes before write |
| Dual-wire | `product_wire=smx2`, `peer_http=lab_ops_only`, `peer_http_is_product_bus=false` |
| Share | `state_matrix_only`, `hold_flash=1` |
| Manager | `nb-manager` wire `smx_motivate`; other bots `smx2` |

On-disk plate schema: `grokium.nanobot_fleet.v1` under `data/home/FLEET.json`.
CLI status schema: `grokium.fleet_status.v1`.

`make -C c_core test-fleet` / `grokium-fleet selftest` checks defaults, note-pid
live vs dead, reload, alive count, and dual-wire plate fields.

## Loopback control plane (lab/ops HTTP, **not** product bus)

```bash
./scripts/grokium serve              # 127.0.0.1:17444 (or free port)
./scripts/grokium filter allow-check 'NEXUS_COORD v1 | type=heartbeat |'
```

| Endpoint | Role |
|----------|------|
| `GET /v1/nanobot/status` | fleet plate (`grokium.nanobot_status.v1`) + kill(0) |
| `POST /v1/nanobot/deploy` | deploy homes + honest offline plate |
| `POST /v1/nanobot/spawn` | body = id, `{"id":…}`, or empty = all |
| `POST /v1/nanobot/separate` | SIGTERM one bot; body = id or `{"id":…}` |

Spawn/deploy/separate replies carry the same dual-wire honesty keys as status.

| Plane | Protocol | Role |
|-------|----------|------|
| Product talk | SMX2 / NEXUS_COORD | core ↔ external cells |
| Lab control | loopback HTTP (`serve`) + bot peer ports | ops only; never Commander |

Optional CubalC boards: `cubalc/programs/fleet.cubalc` · `fleet_resolve.cubalc`.

Python `nanobots.py` was removed (py=0).
