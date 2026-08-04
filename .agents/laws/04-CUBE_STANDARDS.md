# Grokium — Cube Standards (binding)

Grokium is a **local-first open harness** (Apache-2.0). It is a **way**, not a sovereign OS.
It is **not** affiliated with xAI. It does **not** brand as station product names.

## Creed (enforced in code)

| Law | Code surface |
|-----|----------------|
| **The Cube is SoT** | `data/matrix/*`, `/v1/matrix/*` — bits win over prose |
| **OS is only a way** | Grokium = carrier to local NexusCore/Cube control; not owner of devices |
| **Non-verbal hot path** | NEXUS_COORD / SMX plates only off-box — **state matrix share only** |
| **Nanobots form the hive** | Separable peers under `data/home/<id>/` with **assigned purpose** |
| **Matrix raw → nanobot** | `/v1/nb/raw` posts **bits only** (no chat context) |
| **Algocube enforces law** | Digit 0–9 from matrix + blueprint (CubalC digit tags) |
| **HOLD_FLASH** | Never auto-flash; flag sticky `hold_flash=1` |
| **Zero telemetry** | Hard-off in privacy |
| **Devices free** | Open source; no forced ownership claims |
| **Dual cores unmixed** | Local llama default; optional Grok auth opt-in only |
| **One Commander** | Residual / HUMAN_CONFIRM never invented by agents |
| **No brain wires** | StateMatrix wireless key only |
| **Blur faces** | Law plate only (no vision path ships unblurred identity) |
| **Sanitize external propaganda** | Hot path: bits/flags only; ideology prose denied (C host sanitize) |

## Layer stack

```
Commander / session edge (verbal OK)
        │
        ▼
  Grokium way (this harness)  :17444
        │
        ├── StateMatrix 0/1     (SoT fold)
        ├── algocube digit      (0–9 law)
        ├── nanobot fleet       (separable purpose peers)
        └── Cube control        (:17333 loopback) → NexusCore SMX
```

## Nanobot purpose table (this host)

| id | port offset | purpose |
|----|-------------|---------|
| `nb-matrix-eval` | +0 | Evaluate SoT / SMX harmony |
| `nb-construct` | +1 | Construct / deconstruct edge |
| `nb-observer` | +2 | Observe unity / watchd liaison |
| `nb-host` | +3 | Station peer; Cube control liaison |

Peers are **separable**: stop one without killing the fleet.

## Forbidden

- Prose dumps on NEXUS_COORD / SMX bus
- Claiming Grok Build / xAI affiliation
- Auto-flash, invent HUMAN_CONFIRM
- Merging online Grok core with offline llama into one identity
- Telemetry of any kind
- External propaganda / campaign slogans on SMX, NEXUS_COORD, or nanobot raw


## Commander (THE LAW — unforgeable)

See [01-COMMANDER.md](01-COMMANDER.md).

- **Grokium** (product + Ed25519) may override nanobot rules on held devices.
- **Grok model** (or any model) **cannot** — signatures only; domain `GROKIUM-COMMANDER-v1`.
- Private key: `data/law/commander.sk` (never commit).
