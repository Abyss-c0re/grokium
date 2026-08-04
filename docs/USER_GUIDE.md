# Grokium — User Guide

## Not affiliated with xAI or Grok

Grokium is **independent**. It is **not** an xAI product, **not** official Grok Build,
and **not** endorsed or sponsored by xAI. Trademarks “Grok” / “xAI” appear only where
needed for optional interoperability (API, session import). See root `NOTICE`.


**Grokium** is a local-first agent harness (Apache-2.0).  
It is **not** affiliated with xAI. The **TUI is the main UI**; web is optional.

## Install / run

```bash
cd /path/to/grokium
./scripts/grokium              # primary TUI
./scripts/grokium tui          # same
./scripts/grokium serve        # optional HTTP API + web UI :17444
```

Needs:

- # no Python — CubalC + C host
- **llama.cpp** OpenAI server for local models (default `http://127.0.0.1:1212/v1`)
- Optional: `GROK_API_KEY` or `XAI_API_KEY` for cloud Grok

## Choose a llama.cpp model

Models are configured in **`config/models.toml`** (aliases) and must be **loaded** in your llama-server.

### In the TUI

```text
/model list          # presets + what the server currently serves
/model qwen          # select by alias (from models.toml)
/model local         # first model the server has loaded
/model <full-id>     # exact id from /v1/models (often the GGUF path)
```

Selection is **persisted** in `data/model_pref.json`.

### CLI

```bash
./scripts/grokium models list
./scripts/grokium models show
./scripts/grokium models set qwen
```

### Config

`config/grokium.toml`:

```toml
[local]
base_url = "http://127.0.0.1:1212/v1"
model = "qwen"          # alias or id
models_file = "…/config/models.toml"
gguf_dir = "${GGUF_DIR:-~/models}"
```

`config/models.toml`:

```toml
[[models]]
alias = "qwen"
label = "Qwen3.5 9B …"
id = "${GGUF_DIR:-~/models}/….gguf"
backend = "local"
```

### Environment

```bash
export GROKIUM_LOCAL_MODEL=qwen
# or full server id:
export GROKIUM_LOCAL_MODEL=${GGUF_DIR:-~/models}/YourModel.gguf
```

### Important

**llama-server only answers with models it has loaded.**  
Grokium can *select* an id; it does not replace loading GGUFs into llama-server.  
Start/reload the server with `-m path.gguf` (or your multi-model setup), then `/model list` to confirm **live** ids.

## Versions (two different things)

| Version | Meaning |
|---------|---------|
| **Grokium** (`0.x`) | Our app / product version |
| **Reported Grok Build** | Sent as `x-grok-client-version` to cli-chat-proxy only |

A background watcher (default every **3 hours**) refreshes the *reported* version
from the local `grok` CLI / `~/.grok/version.json` / GitHub when reachable, and
**hot-swaps live** — no TUI restart.

```bash
./scripts/grokium compat           # status
./scripts/grokium compat refresh   # check now
# TUI: /compat  ·  /compat refresh
```

Env: `GROKIUM_GROK_BUILD_VERSION=0.2.112` force · `GROKIUM_VERSION_WATCH_SEC=10800` interval.

## Grok auth (same token store as original CLI)

Grokium is **not** xAI software. For optional cloud Grok it reuses the **same**
credential store as the original Grok Build CLI:

| Step | What happens |
|------|----------------|
| Already logged in with `grok login` | Grokium reads `~/.grok/auth.json` automatically |
| Or API key | `export GROK_API_KEY=...` |
| Need browser login | TUI `/login` or `./scripts/grokium login` runs **`grok login`** (original web/OIDC flow) |

```bash
# status (never prints the token)
./scripts/grokium auth

# reuse existing web session or open browser via original CLI
./scripts/grokium login

# TUI
/auth
/login
/backend grok
/logout          # back to llama.cpp; does not delete auth.json
```

## Backend: local vs Grok cloud

```text
/backend local     # llama.cpp (default)
/backend grok      # cloud — needs API key
/backend           # status
```

```bash
export GROK_API_KEY=...    # or XAI_API_KEY
./scripts/grokium
# /backend grok
```

Cores stay **unmixed** (no silent cloud fallback).

## Everyday TUI commands

| Command | Purpose |
|---------|---------|
| `/help` | Help |
| `/new` | Clear chat context |
| `/sessions [q]` | Imported session **metas** (`data/import`) |
| `/pickup` `/load <id>` | Meta + host-local history resume (not product bus) |
| `/mode chat\|agent\|resume` | Tools on/off; resume = host-local TUI |
| `/model …` | Models (above) |
| `/backend …` | local / grok |
| `/theme crimson\|matrix\|void\|gold\|mono` | Look |
| `/law` | Cube Standards plate (share=state_matrix_only) |
| `/status` | Dual-wire honesty (fleet + matrix; SMX2 ≠ peer HTTP) |
| `/fleet [status…]` | Pure-C fleet plate (honest pid; peer HTTP lab_ops) |
| `/manager [DIR]` | Motivate incomplete contracts (nb-manager) |
| `/contract form\|validate…` | External cell contracts (SMX filter) |
| `/integrity` | CODE_SEAL + privacy fail-closed tick |
| `/commander` | Ed25519 law fingerprint (≠ model) |
| `/coord <NEXUS_COORD plate>` | Fold plate → StateMatrix (SMX filter) |
| `/smx` | Latest SMX bits |
| `/quit` | Exit |

Product bus = **SMX2**; peer HTTP = lab/ops only. Session resume is **host-local**
(TUI display + nanobot recent memory seed) — never the SMX product bus.

**Enter** sends · **PgUp/PgDn** scroll · **Tab** focus sessions.

## Optional web UI

```bash
./scripts/grokium serve
# open http://127.0.0.1:17444/
```

Same law: zero telemetry, SMX share only, loopback bind.

## Privacy (short)

- Telemetry hard-off  
- External share = **StateMatrix only**  
- Grok **model** is not **Grokium commander** (crypto identity is separate)  
- See [`.agents/laws/`](../.agents/laws/) (Cube Laws)

## Troubleshooting

| Symptom | What to try |
|---------|-------------|
| llama cold | Is server up on `:1212`? `curl http://127.0.0.1:1212/v1/models` |
| model not found | `/model list` — use a **live** id; load GGUF in llama-server |
| Grok fails | Export API key; `/backend grok` |
| Integrity FAIL after edit | `./scripts/grokium integrity reseal` (intentional) |
| Colors missing | Terminal must support colors; `/theme mono` |

## More docs

| Doc | Audience |
|-----|----------|
| [USER_GUIDE.md](USER_GUIDE.md) | You are here |
| [DEVELOPER.md](DEVELOPER.md) | Extending Grokium |
| [BACKENDS.md](BACKENDS.md) | local vs Grok |
| [TUI.md](TUI.md) | TUI reference |
| [THEMES.md](THEMES.md) | Themes |
| [API_AND_MCP.md](API_AND_MCP.md) | HTTP + MCP |
| [`.agents/laws/`](../.agents/laws/) | Cube Laws (tracked) |


## Failover / backup (session-safe)

When primary backend is down, Grokium can try the other (default **auto**):

```text
/failover auto              # active first, other as backup
/failover none              # no backup
/failover local_then_grok
/failover grok_then_local
```

```bash
export GROKIUM_FAILOVER=auto
```

### `/model` shows both

```text
/model              # list local llama + grok, ★ = active
/model local        # switch to llama.cpp — chat history kept
/model grok         # switch to Grok — chat history kept
/model qwen         # local alias
```

Switching **does not** run `/new` — conversation continues on the new backend.
