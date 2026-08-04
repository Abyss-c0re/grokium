# AGENTS — Grokium

Not affiliated with xAI. Product: **Grokium**. Core: **nanobot** (embeddable C).

## Read first

1. [laws/00-THE_LAW.md](laws/00-THE_LAW.md)
2. [laws/01-COMMANDER.md](laws/01-COMMANDER.md)
3. [laws/02-INTEGRITY.md](laws/02-INTEGRITY.md)
4. [laws/03-SANITIZE.md](laws/03-SANITIZE.md)
5. [laws/04-CUBE_STANDARDS.md](laws/04-CUBE_STANDARDS.md)

## Stack

| Layer | Path | Role |
|-------|------|------|
| Host TUI | `host/` | Desktop UX, config, hub, version watch |
| Agent core | `deps/nanobot` | Chat, tools, MCP, LLM gate, **braincells** |
| Decision core | `deps/braincube` | LHLAM BrainCube — hive route/fuse |
| Board | `cubalc/programs/` | Optional fleet/law CubalC |
| Config | `config/` | Local llama + hub slots |

## Hard rules

- **Local-first** — default `http://127.0.0.1:1212/v1` (llama.cpp).
- **LLM hub gate** — shared lock; do not stampede the server.
- **No telemetry** — privacy hard-off.
- **No xAI branding as product** — interop only, honest User-Agent.
- **Commander ≠ model** — only Ed25519 under `data/law/commander.pk`.
- **nanobot stays embeddable** — no ncurses in core; tidy commits only.
- **BrainCube decides; nanobots are braincells** — see laws/05-BRAINCELL_HIVE.md.
- **py=0** — no Python product path.

## Secrets

Never commit `data/law/commander.sk`, tokens, or `auth.json`.
