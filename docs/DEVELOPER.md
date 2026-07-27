# Grokium — Developer Guide

## Layout

```
grokium/
  config/
    grokium.toml      # main config
    models.toml       # model presets (aliases → server ids)
    mcp.grok.toml.snippet
  src/grokium/
    tui.py            # primary UI (curses)
    llm.py            # backends + streaming
    models.py         # model catalog / resolve / persist
    agent.py          # tool loop
    sessions.py       # import Grok Build sessions
    integrity_core.py # anti-collection
    smx_stream.py     # realtime StateMatrix bus
    commander.py      # Ed25519 commander (calls C binary)
    themes.py         # TUI/web themes
    md_render.py      # markdown → curses segments
    server.py         # optional HTTP
    mcp_server.py     # optional MCP stdio
    nanobots.py       # fleet (not chat core)
  c_core/             # commander Ed25519 (libsodium)
  www/                # optional web UI
  docs/               # user + law + API
  data/               # runtime (prefs, matrix, integrity) — mostly gitignored
  scripts/grokium     # entry
```

## Principles

1. **TUI primary**, web/API optional  
2. **Zero telemetry** (hard-false privacy)  
3. **Local llama default**; Grok opt-in  
4. **Configurable models** via `models.toml` + live `/v1/models`  
5. **Nanobot ≠ chat core** (integrity + fleet only)  
6. **Cube**: SMX share only, HOLD_FLASH, commander crypto  
7. **Apache-2.0** + NOTICE / THIRD-PARTY-NOTICES  

## Models system (`models.py`)

Resolution order for local model id:

1. Runtime `/model` or `set_model()`  
2. `GROKIUM_LOCAL_MODEL` / `GROKIUM_MODEL`  
3. `data/model_pref.json`  
4. `[local].model` in `grokium.toml`  
5. `[defaults].local_model` in `models.toml`  
6. First live server model (`id == "local"`)

Alias table lives in **`config/models.toml`**.  
**Server must already expose the id** (llama-server load).

```python
from grokium.models import list_all, resolve_model_id, set_model, persist_model
from grokium.config import load
cfg = load()
print(list_all(cfg))
set_model("qwen")
print(resolve_model_id(cfg))
```

When adding a model:

1. Load GGUF into llama-server  
2. `curl :1212/v1/models` → copy exact `id`  
3. Add `[[models]]` alias in `models.toml`  
4. Optional default: `[local] model = "your_alias"`  

## LLM (`llm.py`)

- `set_backend("local"|"grok")` / `get_backend(cfg)`  
- `chat(...)` non-stream  
- `chat_stream(..., on_token=)` SSE-style deltas  
- Egress gated by `privacy.guard_url` + integrity allowlist  

## TUI extension

- Slash commands in `GrokiumTUI.run_command`  
- Themes: `themes.py` + `init_curses_theme`  
- Markdown: `md_render.render_markdown_lines`  
- Prefer small pure modules over growing `tui.py` forever  

## HTTP API / MCP

See [API_AND_MCP.md](API_AND_MCP.md).  
New features: add route in `server.py` **and** tool in `mcp_server.py` if user-facing.

## Integrity

After intentional edits to sealed modules:

```bash
./scripts/grokium integrity reseal
./scripts/grokium integrity check
```

Sealed paths listed in `integrity_core.CRITICAL_REL`.  
`GROKIUM_INTEGRITY_STRICT=1` (default) fails closed on code seal mismatch.

## Commander (C)

```bash
make -C c_core
./build/grokium-commander keygen --law-dir data/law
```

Python: `commander.py` wraps the binary.  
**Never commit** `data/law/commander.sk`.

## CubeOS / visualization

For Cube-compatible manifestation of state (optional):

- Write **`cube.viz_frame.v1`** JSON to  
  `lab/prophecy_cube/state/viz_frame.json` (schema: cubes[] with x,y,z,s,rgba,role)  
- LOVR polls that path (see `lab/prophecy_cube/lovr/main.lua`)  
- SMX stream under `data/matrix/stream/` is Grokium-local; fold to station via NEXUS_COORD / Cube API as needed  

Budget: keep `n_cubes` ≤ ~40 for lean LOVR path.

## Tests / smoke

```bash
./scripts/grokium models list
./scripts/grokium models show
./scripts/grokium llama-test
./scripts/grokium integrity check
./scripts/grokium selftest   # broader
```

## License / branding

- Product name **Grokium**  
- Do not brand as ProjectNexus in this tree  
- Credit Grok Build as inspiration only (Apache-2.0 upstream)  
- Third-party: nanobot MIT (host binary), libsodium ISC, llama.cpp MIT (host)  

## Git hygiene

```
data/law/commander.sk   # secret
data/import/            # large session trees
data/home/              # nanobot homes
data/runs/              # logs
build/
```

See `.gitignore`.
