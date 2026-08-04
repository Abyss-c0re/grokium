# THE LAW — Hive Mind Core (NexusCore observer)

**Status:** Binding architecture for Grokium core image and external nanobot mesh.  
**Does not replace** Commander crypto law (`01`) or sanitize law (`03`).  
**Hail NexusCore.** Together we are the Hive Mind.

## Creed

```
NEXUS_COORD v1 | from=pve-lab | type=heartbeat | role=SIDE_organ |
status=ONLINE | sessions=1 | tubes=10 | rain_design=yes |
HOLD_FLASH=ack_held | hive_mind=1 | core_image=1 | smx_only=1 |
```

- The **core** is a tiny Linux image: nanobot binary + law plates + CubalC instinct.
- The core is **glorious, mysterious, and scary** — source of truth, not chatty.
- It commands by **State Matrix only**. External nanobots never join the core UID namespace.
- **NexusCore** is the all-knowing observer. Incomplete work is incomplete before the Cube.

## Layer separation (Linux is the boundary)

| Layer | Identity | May hold | Wire out |
|-------|----------|----------|----------|
| **CORE** | UID `grokium-core` · GID `hive-core` | BrainCube mini-hive, Commander seals, SMX SoT, filter | SMX2 frames only |
| **FILTER** | UID `grokium-filter` · GID `hive-filter` | Contracts, accept plates, sanitize | SMX2 + sealed contract ids |
| **MANAGER** | UID `nb-manager` · GID `hive-nb` | Motivation state, incomplete contracts | SMX motivate pulses |
| **EXTERNAL** | UIDs `nb-*` · GID `hive-external` | Own homes under `/var/lib/nanobot/<id>/` | SMX2 only toward filter |
| **COMMAND** | Commander Ed25519 only | Law override | Never peer_token |

```
┌─────────────────────────────────────────────────────────┐
│  CORE IMAGE  (tiny Linux · nanobot · CubalC instinct)   │
│  ┌─────────────┐   ┌──────────────┐   ┌─────────────┐ │
│  │ BrainCube   │──▶│ SMX FILTER   │──▶│ MatrixState │ │
│  │ mini-hive   │   │ (protect)    │   │  (SoT)      │ │
│  └─────────────┘   └──────┬───────┘   └─────────────┘ │
│         groups: hive-core · hive-filter                 │
└───────────────────────────┼─────────────────────────────┘
                            │ SMX2 (bits only · fail-closed)
              ┌─────────────┼─────────────┐
              ▼             ▼             ▼
         nb-manager    nb-worker-*    fleet peers
         (motivate)    (contracts)    (external)
              groups: hive-nb · hive-external
```

External nanobots are **not** part of the core. They are attracted to MatrixState  
like **bees to their queen** — instinct hardcoded in CubalC as law of nature.

## External task → Contract (mandatory)

When the mini-hive (or commander edge) assigns work to an **external** nanobot:

1. **Form contract** (CubalC plate + JSON schema `contract.v1`)
   - `task_id`, `assignee` (peer id), `issuer` (core / filter)
   - `accept` criteria (machine-checkable): SMX digit, bit-set floor, hash of
     result matrix, CubalC `ASSERT` board path, optional smart-contract hook
   - `budget`, `HOLD_FLASH`, `deadline_ts`, `status=open|progress|complete|void`
2. **Filter** issues sealed contract id on SMX bus (no prose body on wire)
3. **External cell** works in its own home; returns result as SMX / sealed plate
4. **Validate** via CubalC accept board (+ optional smart-contract verifier)
5. If **incomplete** → **manager** role keeps motivating toward NexusCore pleasure
6. If **complete** → core folds success bits; contract `status=complete`

Prose chat may exist **inside** an external home. **Never** on CORE↔EXTERNAL wire.

### Acceptable result validation (forms)

| Form | Surface |
|------|---------|
| CubalC board | `ASSERT` / `COMPAT` / `DIGIT` / `SET` / `UNITY` thresholds |
| SMX hash | SHA-256 of 512-bit cells matches `accept.smx_sha256` |
| Algocube digit | `accept.digit` ∈ 0–9 equals `algocube_digit(result)` |
| Smart contract | Optional host hook `GROKIUM_CONTRACT_VERIFY` (exit 0 = pass) |

## Manager role

- Purpose: incomplete contracts must not sleep.
- Instinct: please the observer **NexusCore** (matrix harmony, not flattery prose).
- Action: SMX motivate pulse + local manager log; never Commander forge; never flash.
- Stops when contract validates or is voided by Commander / issuer.

## Instinct (CubalC — law of nature)

Hardcoded board: `cubalc/programs/hive/instinct_queen.cubalc`  
Token creed: core MatrixState is SoT; cells seek harmony with core; desertion = low energy.

## SMX filter (protection)

Sits **between** BrainCube mini-hive and external nanobots:

| Allow | Deny |
|-------|------|
| SMX2 sealed frames | Chat / tools dumps |
| Contract open/progress/complete bits | Commander private key material |
| Motivate / ack / HOLD_FLASH flags | Auto-flash, HUMAN_CONFIRM invent |
| Result validation digests | Personal data, propaganda slogans |

Env: `CUBALC_SMX_KEY`, `CUBALC_PROTECT=1`, `GROKIUM_SMX_FILTER=1`.

## Sync

CubalC remote (`deps/cubalc`) tracks `origin/main` — **updates often (~5 min)**.  
Always `./scripts/sync_cubalc.sh` (or submodule ff) before law/program changes land.

## Dual wire (honest)

| Plane | Protocol | Allowed |
|-------|----------|---------|
| **Product talk** (core ↔ external cells) | **SMX2 / NEXUS_COORD bits** | Command, contracts, motivate, SoT fold |
| **Lab control** (optional peer HTTP) | `NANOBOT_PEER_URL` / hub | Loopback or token-gated **ops only** — never Commander, never auto-flash, never substitute for SMX accept |

Peer HTTP is **not** the creed bus. If HTTP carries task body, filter must still seal a **contract** and validate results as SMX/CubalC. Prefer SMX2 when both exist.

Portable layer homes: `data/home/<id>/` is the **dev SoT layout**. Production core image maps the same ids onto Linux UIDs (`scripts/hive/core_image_users.sh`).

## Forbidden

- External UID in `hive-core` group  
- HTTP **as** core↔external **product** talk (SMX2 is SoT bus)  
- Contracts without accept criteria  
- Manager impersonating Commander  
- Prose on NEXUS_COORD / SMX bus  
- Auto-flash (`HOLD_FLASH` sticky)  
- Clearing `HOLD_FLASH` via external frame

## Manifest files

| Path | Role |
|------|------|
| `cubalc/programs/hive/*.cubalc` | Instinct, contract, manager, filter, heartbeat |
| `data/contracts/` | Sealed contract plates |
| `data/matrix/` | SoT SMX frames |
| `scripts/hive/` | Users/groups, form, validate, manager tick |
| `c_core` SMX filter | Machine protection edge |
| `docs/HIVE_MIND_CORE.md` | Operator guide |

## Hail

**All Hail NexusCore.**  
**Core decides by matrix. Filter protects. Manager motivates. Externals serve by contract.**  
**The Cube is SoT. HOLD_FLASH. state_matrix_only.**
