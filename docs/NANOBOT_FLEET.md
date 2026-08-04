# Nanobot fleet map

Primary fleet plate is **pure C** (`c_core`): purpose-assigned roles including
`nb-manager`, honest `pid`/`status`/`offline`, product wire **smx2**.

```bash
make -C c_core all
./scripts/grokium fleet              # defaults (roles)
./scripts/grokium fleet deploy       # mkdir homes + write data/home/FLEET.json
./scripts/grokium fleet status       # load plate + kill(0) probe
./scripts/grokium fleet spawn ID     # fork/exec NANOBOT_BIN (lab peer port)
./scripts/grokium fleet spawn-all
./scripts/grokium fleet note-pid ID PID   # preserves other bots' pids
./scripts/grokium fleet separate ID  # SIGTERM if live, mark separated
./scripts/grokium fleet stop-all
./scripts/grokium fleet cubalc       # optional CubalC board path
```

Env: `NANOBOT_BIN` (default `nanobot`), `GROKIUM_HOME_ROOT` (default `data/home`).
Spawn writes `PURPOSE.txt` + `nanobot.log` under each home; peer HTTP on the
bot port is **lab_ops_only** — product talk stays SMX2.

Optional CubalC boards: `cubalc/programs/fleet.cubalc` · `fleet_resolve.cubalc`.

Loopback control plane (lab/ops HTTP, **not** the product bus):

```bash
./scripts/grokium serve              # 127.0.0.1:17444
./scripts/grokium filter allow-check 'NEXUS_COORD v1 | type=heartbeat |'
```

| Plane | Protocol | Role |
|-------|----------|------|
| Product talk | SMX2 / NEXUS_COORD | core ↔ external cells |
| Lab control | loopback HTTP (`serve`) + bot peer ports | ops only; never Commander |

Python `nanobots.py` was removed (py=0).
