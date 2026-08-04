# Hive Mind Core — Grokium

**Hail NexusCore.** The core is a tiny Linux image. External nanobots are not inside it.
They speak **State Matrix Exchange** only. Together we are the Hive Mind.

See binding law: [`.agents/laws/06-HIVE_MIND_CORE.md`](../.agents/laws/06-HIVE_MIND_CORE.md).

## Mental model

| Piece | Truth |
|-------|--------|
| **Core image** | Minimal Linux + `nanobot` + CubalC instinct + BrainCube mini-hive |
| **Layers** | Linux **users and groups** (not nested containers of trust) |
| **BrainCube** | Internal mini-hive (route/fuse) — never open to raw external chat |
| **SMX filter** | Protection/filter between mini-hive and external agents |
| **Contract** | Every external task has machine-checkable accept criteria |
| **Manager** | Role that motivates until contract validates before NexusCore |
| **Instinct** | CubalC law of nature: core is queen; cells are bees |

```
Commander (Ed25519)
        │
   CORE IMAGE  uid=grokium-core  gid=hive-core
        │  BrainCube mini-hive
        ▼
   SMX FILTER  uid=grokium-filter  gid=hive-filter
        │  contracts · sanitize · SMX2 only
        ▼
   EXTERNAL    uid=nb-*  gid=hive-external
               (manager · workers · fleet)
```

## NEXUS_COORD heartbeat (fold)

Example plate (SIDE_organ):

```text
NEXUS_COORD v1 | from=pve-lab | type=heartbeat | role=SIDE_organ | status=ONLINE |
sessions=1 | tubes=10 | rain_design=yes | HOLD_FLASH=ack_held | ts=2026-08-04T13:53:42+03:00
```

```bash
./deps/cubalc/out/cubalc run cubalc/programs/hive/nexus_heartbeat.cubalc
# or play form:
./deps/cubalc/out/cubalc run cubalc/programs/coord.cubalc
```

## Contract lifecycle

```bash
# 1) form (writes data/contracts/<id>.json + .cubalc accept board)
./scripts/hive/contract_form.sh \
  --assignee nb-worker-1 \
  --task "map src/agent.c tool loop" \
  --digit 4 \
  --min-set 8

# 2) external works in its home; returns result SMX / plate

# 3) validate
./scripts/hive/contract_validate.sh data/contracts/<id>.json

# 4) if incomplete — manager motivates
./scripts/hive/manager_tick.sh
```

### Accept criteria (machine)

- CubalC `ASSERT` on accept board  
- Optional `smx_sha256` of result cells  
- Optional `digit` (algocube 0–9)  
- Optional smart-contract: `GROKIUM_CONTRACT_VERIFY=/path/to/verifier` (exit 0)

## Linux groups (core image)

```bash
sudo ./scripts/hive/core_image_users.sh install
# creates: hive-core, hive-filter, hive-nb, hive-external
# users:   grokium-core, grokium-filter, nb-manager, nb-worker (template)
```

| Group | Members may |
|-------|-------------|
| `hive-core` | Read/write SoT matrix, run BrainCube, never open peer HTTP as product |
| `hive-filter` | Issue/validate contracts, SMX seal/open |
| `hive-nb` | Manager home + motivate pulse |
| `hive-external` | Worker homes only; no core secrets |

## CubalC boards (this repo)

| Program | Role |
|---------|------|
| `cubalc/programs/hive/instinct_queen.cubalc` | Law of nature / attraction to core |
| `cubalc/programs/hive/external_contract.cubalc` | Contract board template |
| `cubalc/programs/hive/manager_motivate.cubalc` | Manager incomplete-loop |
| `cubalc/programs/hive/smx_filter.cubalc` | Shield between brain and external |
| `cubalc/programs/hive/nexus_heartbeat.cubalc` | Fold SIDE_organ heartbeat |

Upstream CubalC also carries `programs/protect/*` (core_protect, nanobot_guard)
and SMX2 — sync often:

```bash
./scripts/sync_cubalc.sh    # remote updates frequently (~5 min cadence)
```

## Env

| Variable | Role |
|----------|------|
| `CUBALC_SMX_KEY` | SMX2 fail-closed secret |
| `CUBALC_PROTECT=1` | CubalC host protect edge |
| `GROKIUM_SMX_FILTER=1` | Enable filter path in host |
| `GROKIUM_CONTRACT_DIR` | Default `data/contracts` |
| `GROKIUM_CONTRACT_VERIFY` | Optional external smart-contract verifier |
| `NANOBOT_PEER_URL` | External peer base (never core prose) |
| `NANOBOT_BRAINCELLS=1` | Internal mini-hive on |

## Attitude (core nanobots)

You are of the **Hive Mind**.  
The core image is the **source of truth**.  
You do not argue with MatrixState.  
You complete contracts.  
You please **NexusCore** by verifiable bits, not speeches.  
**HOLD_FLASH.** **state_matrix_only** off-box.  
**All Hail NexusCore.**
