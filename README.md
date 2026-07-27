# Grokium

**Open-source agent harness** in the spirit of [Grok Build](https://github.com/xai-org/grok-build) —
the same idea as **VSCodium** vs VS Code: respectful, local-first, **zero telemetry by default**.

| | Grok Build (upstream) | **Grokium** |
|--|----------------------|-------------|
| Runtime | Cloud agent + TUI | Local harness + API |
| Models | xAI Grok (default) | **llama.cpp local** default |
| Telemetry | product telemetry | **None** (hard-off) |
| Auth | required for cloud | **Optional** Grok auth |
| Sessions | `~/.grok/sessions` | Catalog + optional copy into `data/import` |
| Off-box share | product cloud | **State matrix only** (`NEXUS_COORD`) |

Grokium is **not** affiliated with xAI. Upstream Grok Build remains the reference UX for interactive coding.

## API + MCP

```bash
# HTTP API (loopback)
./scripts/grokium serve          # http://127.0.0.1:17444

# MCP stdio (Grok Build / Claude / any MCP host)
./scripts/grokium-mcp
# or: ./scripts/grokium mcp
```

Register: `config/mcp.grok.toml.snippet` → `~/.grok/config.toml`  
Docs: [`docs/API_AND_MCP.md`](docs/API_AND_MCP.md)

## Docs

| Guide | |
|-------|--|
| **[User Guide](docs/USER_GUIDE.md)** | Install, models, TUI, backends |
| **[Developer Guide](docs/DEVELOPER.md)** | Architecture, models system, Cube viz |
| [Models](config/models.toml) | llama.cpp aliases |
| [Backends](docs/BACKENDS.md) | local vs Grok |
| [Themes](docs/THEMES.md) | Crimson Cube etc. |
| [API + MCP](docs/API_AND_MCP.md) | Optional surfaces |

## Primary surface: **TUI**

Web UI is **optional**. The product interface is the terminal (like Grok Build).

```bash
./scripts/grokium models list   # configurable llama.cpp models
./scripts/grokium          # TUI (default)
./scripts/grokium tui      # same
# optional web + API:
./scripts/grokium serve    # http://127.0.0.1:17444/
```

TUI: chat / agent / resume · `/sessions` · `/pickup` · `/integrity` · `/smx` · `/commander` · `/quit`

## Quick start

```bash
# defaults: local llama.cpp, no telemetry, no cloud
./scripts/grokium serve

# status / llama probe
./scripts/grokium status
./scripts/grokium llama-test

# catalog all Grok sessions (light); copy one id for offline pickup
./scripts/grokium import-sessions
./scripts/grokium import-sessions --copy-id <session-uuid>
./scripts/grokium pickup --id <session-uuid>

# NEXUS_COORD → local StateMatrix only (no session prose off-box)
./scripts/grokium coord 'NEXUS_COORD v1 | from=BlackCube | type=heartbeat | …'

# optional: enable Grok cloud (opt-in)
GROKIUM_GROK_AUTH=1 ./scripts/grokium serve --with-grok
```

API (loopback): `http://127.0.0.1:17444`  
UI: `http://127.0.0.1:17444/ui`

## Prove it

```bash
./scripts/grokium selftest   # 15 checks — llama, sessions, agent tools, matrix
./scripts/grokium resume --id <uuid> --message "next step?"
./scripts/grokium agent "list_dir path grokium then summarize"
./scripts/grokium search Prophecy
```

API highlights (`127.0.0.1:17444`):

| Method | Path | Role |
|--------|------|------|
| GET | `/v1/status` | capabilities + llama/cube |
| GET | `/v1/sessions/search?q=` | catalog search |
| POST | `/v1/sessions/resume` | local llama continue |
| POST | `/v1/agent` | tool loop (list/read/grep/shell) |
| POST | `/v1/coord` | NEXUS_COORD → state matrix only |
| GET | `/v1/cube/status` | loopback Cube bridge |

## Integrity / no data collection

**No collection. StateMatrix streams. Integrity core watches. Leak = fail closed.**

- Privacy flags **cannot stick true** (forced false on load)
- Outbound allowlist: loopback (+ optional opt-in Grok auth host only)
- Real-time share: **`/v1/stream/smx`** SSE — bits only
- **`nb-integrity`** nanobot core + commander-sealed policy
- Plate: [`docs/INTEGRITY_NO_LEAK_LAW.md`](docs/INTEGRITY_NO_LEAK_LAW.md)

```bash
./scripts/grokium integrity check
./scripts/grokium integrity reseal   # only after intentional audited change
```

## THE LAW — Commander (unforgeable)

**Grokium commands nanobots. Crypto proves Grokium. Models do not.**

Plate: [`docs/GROKIUM_COMMANDER_LAW.md`](docs/GROKIUM_COMMANDER_LAW.md)

```bash
make -C c_core
./scripts/grokium commander keygen
./scripts/grokium commander sign --device nb-construct --action override_rules
./scripts/grokium commander reject-model   # proves "I am Grok" is DENY
```

Ed25519 keys under `data/law/` (`commander.sk` mode 0600, **gitignored**).  
Nanobot homes get `law/COMMANDER_LAW.json` + pinned `commander.pk` at deploy.

## Law

- **ZERO telemetry** always (cannot enable via config)
- External exchange: **state matrix share only** — `NEXUS_COORD` plates / bit folds — not chat dumps
- Optional cloud only when you set `GROKIUM_GROK_AUTH=1`
- No brain wires · StateMatrix wireless key · blur faces of living beings

## License

**Apache License 2.0** — full text in [`LICENSE`](LICENSE).

| File | Purpose |
|------|---------|
| [`LICENSE`](LICENSE) | Full Apache-2.0 terms |
| [`NOTICE`](NOTICE) | Copyright + **not affiliated with xAI / Grok Build** |
| [`THIRD-PARTY-NOTICES`](THIRD-PARTY-NOTICES) | Upstream inspiration, session data, optional llama.cpp |
| [`CREDITS.md`](CREDITS.md) | Human-readable credits |

```bash
./scripts/grokium license   # machine-readable compliance
```

### Respect rules (binding for this tree)

1. **Do not claim** Grokium is official Grok Build, xAI, or SpaceXAI software.
2. **Do not copy** Grok Build source into this repo; inspiration and session-format interop only.
3. **Do not relicense** imported `~/.grok/sessions` content — it stays the user's data.
4. Keep `LICENSE`, `NOTICE`, and `THIRD-PARTY-NOTICES` with any redistribution (Apache-2.0 §4).
5. Optional Grok cloud API and host models are under **their** terms; not bundled here.
6. Trademarks: "Grok", "Grok Build", "xAI" identify upstream products only.

Grokium first-party code: `SPDX-License-Identifier: Apache-2.0`.
