# Grokium — User Guide

## Not affiliated with xAI or Grok

Grokium is **independent**. It is **not** an xAI product, **not** official Grok Build,
and **not** endorsed or sponsored by xAI. Trademarks “Grok” / “xAI” appear only where
needed for optional interoperability (API, session import). See root `NOTICE`.


**Grokium** is a local-first agent harness (Apache-2.0).  
It is **not** affiliated with xAI. The **TUI is the main UI**; web is optional.

## Install / run

```bash
cd /home/voldemar/Dev/grokium
./scripts/grokium              # primary TUI
./scripts/grokium tui          # same
./scripts/grokium serve        # optional HTTP API + web UI :17444
```

Needs:

- Python 3.11+
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
gguf_dir = "/home/voldemar/Dev/AI/model"
```

`config/models.toml`:

```toml
[[models]]
alias = "qwen"
label = "Qwen3.5 9B …"
id = "/home/voldemar/Dev/AI/model/….gguf"
backend = "local"
```

### Environment

```bash
export GROKIUM_LOCAL_MODEL=qwen
# or full server id:
export GROKIUM_LOCAL_MODEL=/home/voldemar/Dev/AI/model/YourModel.gguf
```

### Important

**llama-server only answers with models it has loaded.**  
Grokium can *select* an id; it does not replace loading GGUFs into llama-server.  
Start/reload the server with `-m path.gguf` (or your multi-model setup), then `/model list` to confirm **live** ids.

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
| `/sessions [q]` | Search imported Grok Build sessions |
| `/load <id\|q>` | Pick up session (resume mode) |
| `/mode chat\|agent\|resume` | Mode |
| `/model …` | Models (above) |
| `/backend …` | local / grok |
| `/theme crimson\|matrix\|void\|gold\|mono` | Look |
| `/integrity` | Anti-collection seal |
| `/coord <NEXUS_COORD plate>` | Fold plate → StateMatrix |
| `/smx` | Latest SMX bits |
| `/quit` | Exit |

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
- See `docs/INTEGRITY_NO_LEAK_LAW.md` and `docs/GROKIUM_COMMANDER_LAW.md`

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
| [CUBE_STANDARDS.md](CUBE_STANDARDS.md) | Cube law mapping |
