# Developer — nanobot core + C host

**Not affiliated with xAI.** Product name is Grokium. Agent core is **nanobot**.

Cube Laws: [`.agents/`](../.agents/) · [AGENTS.md](../AGENTS.md)

## Layout

```
.agents/              Cube Laws + AGENTS.md (tracked in git)
host/                 Pure C TUI + config + hub (no Python UI)
config/               config.toml.example (full knobs)
cubalc/programs/      Optional CubalC board
deps/                 nanobot · braincube · cubalc (submodules)
scripts/              grokium, sync_*, test
docs/                 User & developer guides
```

## Configuration (pure C)

Load order: `config/config.toml` → `~/.grokium/config.toml` (user wins).  
Save: `/settings save` → `~/.grokium/config.toml`.

| Section | Examples |
|---------|----------|
| `[ui]` | theme, multiline, colors, spoilers, composer rows, product_name |
| `[agent]` | tools, braincells, max_turns, timeouts |
| `[hub]` | port, llm_slots |
| `[model.local]` | base_url, context_window, temperature |

TUI: `/settings`, `/settings ui.multiline=false`, `/settings agent.tools=true`, `/settings reload`.
## Build

```bash
./scripts/sync_nanobot.sh
./scripts/sync_cubalc.sh   # optional fleet/board
make -C deps/nanobot host  # or via top-level make
make -C host all
make test
./scripts/grokium -p 'hello'   # local llama default :1212
./scripts/grokium              # TUI
```

## Split of responsibility

| Layer | Owns | Constraints |
|-------|------|-------------|
| **nanobot** | agent loop, tools, MCP, HTTP peer, auth helpers, OpenAI client | Stay embeddable: `make host`, `make arm`, `make static`. No ncurses. |
| **grokium host** | fullscreen TUI, config.toml UX, slash lookalike, version watch | May use ncurses; desktop-only |

When grokium needs a core fix: change nanobot in a **small tidy commit**, test host (+ arm if toolchain), bump `deps/NANOBOT_PIN.txt`. Do not dump TUI into the peer.

## xAI boundary

- Do **not** vendor official `grok` binaries or claim to be Grok Build.
- Optional: `sync_upstream_surface.sh` records version of a *user-installed* CLI for compat/restart — no redistribution.
- Config keys like `[model.local]` are interface compatibility, not trademark use.
- User-Agent should identify **grokium** / **nanobot** honestly.

## Local llama.cpp

Default: `http://127.0.0.1:1212/v1`. Server must load the GGUF; Grokium selects model ids from live `GET /v1/models`.

```toml
[model.local]
base_url = "http://127.0.0.1:1212/v1"
model = "auto"
context_window = 65536

[hub]
enabled = true
port = 8787
llm_slots = 1
```

## LLM hub (anti-stampede)

Grokium + all nanobots share one **LLM request gate**:

| Mechanism | Role |
|-----------|------|
| `NANOBOT_LLM_LOCK` | Shared flock path (`$XDG_RUNTIME_DIR/nanobot-llm.lock`) |
| `NANOBOT_LLM_SLOTS` | Concurrent completions (default 1; match llama parallel) |
| Stream hold | Lock is held for the **whole SSE stream**, not just POST start |
| `grokium hub start` | Spawns nanobot peer (`--offline --hub`) for fleet |

Fleet peers should use the same lock env (or talk to hub `POST /peer/v1/prompt` with `"stream":true`).

```bash
./scripts/grokium hub start
./scripts/grokium hub status
./scripts/grokium -p 'hello'   # streamed, gated
```

## BrainCube mini-hive + external nanobots

BrainCube is an **internal mini-hive** (decision lattice + local cell routing).  
It is not the whole fleet — it **can** reach **external nanobots** on the peer bus.

| Piece | Role |
|-------|------|
| `deps/braincube` | LHLAM mini-hive core (`lhlam_cube_decide*`) — route/fuse only |
| `deps/nanobot` | Host: local **braincells** (subagents) + peer HTTP for external bots |
| `NANOBOT_BRAINCELLS=1` | Enable mini-hive (Grokium desktop default) |
| `NANOBOT_PEER_URL` | Optional external peer base URL (hive signals / remote capacity) |

Flow for non-trivial coding prompts:

1. Mini-hive routes **SOLO** vs **HIVE**
2. **HIVE (internal):** spawn explore + plan cells → `$NANOBOT_HOME/braincells/*.json`
3. Core fuses reports → implement cell (still local unless peer policy says otherwise)
4. **External (optional):** publish cell events to `NANOBOT_PEER_URL` peer API (token-gated)
5. Final text returns to TUI; tool results still loop back

```bash
./scripts/sync_braincube.sh
./scripts/sync_nanobot.sh   # also inits nanobot's third_party/braincube
make host
```

Submodules: see `.gitmodules` (`deps/nanobot`, `deps/braincube`, `deps/cubalc`).  
Local dev may use sibling symlinks via the sync scripts.  
Law: [`.agents/laws/05-BRAINCELL_HIVE.md`](../.agents/laws/05-BRAINCELL_HIVE.md).

## Long-running agents

| Knob | Default (desktop) | Env |
|------|-------------------|-----|
| Agent tool turns | 64 | `NANOBOT_MAX_TURNS` |
| Shell wall time | 900s | `NANOBOT_CMD_TIMEOUT` |
| LLM HTTP max-time | 600s | `NANOBOT_HTTP_TIMEOUT` |
| Open task ceiling | 96 turns | task board |

Config:

```toml
[agent]
max_turns = 64
cmd_timeout_sec = 900
http_timeout_sec = 600
```

For multi-hour builds: `nohup make -j > build.log 2>&1 &` then poll with `tail`.  
Background `&` / `nohup` use a short shell wait (process detaches).

## Features rules

1. Prefer nanobot for agent/tool/LLM work.
2. Prefer host/ for UX.
3. Prefer CubalC under `cubalc/programs/` for fleet/law board only.
4. Never add Python. Never hardcode machine-local paths in committed config defaults beyond loopback.
