# Grokium API + MCP

## Dual wire (honest)

| Plane | Protocol | Status |
|-------|----------|--------|
| **Product bus** | SMX2 / NEXUS_COORD bits | SoT; filter + contracts |
| **Lab/ops** | loopback HTTP only | `grokium-serve` — not product talk |

Peer HTTP elsewhere is ops-only; never Commander; never auto-flash.

## HTTP API (loopback) — implemented in `c_core`

```bash
make -C c_core all
./scripts/grokium serve          # or: build/grokium-serve
# http://127.0.0.1:17444  (non-loopback bind refused)
```

| Method | Path | Role |
|--------|------|------|
| GET | `/healthz` | liveness |
| GET | `/ui` | minimal lab/ops HTML plate (dual-wire honesty) |
| GET | `/v1/status` | law + dual-wire honesty (`product_wire=smx2`) |
| GET | `/v1/cube/status` | AlgoCube bridge plate (digit/blueprint, dual-wire) |
| GET/POST | `/v1/sessions` | list imported session metas (`?q=` optional) |
| GET/POST | `/v1/sessions/search` | same; body/query `q` |
| GET/POST | `/v1/sessions/pickup` | meta for `id` (query/body); resume messages = host TUI |
| GET | `/v1/sessions/{id}` | meta pickup by id |
| GET | `/v1/law` | law plate |
| GET | `/v1/ability` | consolidator ability card |
| POST | `/v1/coord` | NEXUS_COORD → matrix (SMX filter; dual-wire honesty plate) |
| POST | `/v1/stream/smx/publish` | same as coord |
| GET | `/v1/matrix/latest` | last SMX |
| GET | `/v1/stream/smx/latest` | same as matrix/latest |
| GET | `/v1/stream/smx` | SSE snapshot of latest SMX (bits only; short-lived) |
| GET | `/v1/nanobot/status` | fleet plate (honest pid/status) |
| POST | `/v1/nanobot/deploy` | deploy homes + FLEET.json |
| POST | `/v1/nanobot/spawn` | fork/exec bot(s); body = id, `{"id":…}`, or empty=all |
| POST | `/v1/nanobot/separate` | SIGTERM one bot; body = id or `{"id":…}` |
| POST | `/v1/nanobot/note-pid` | host/hub record pid; body = `ID PID` or `{"id","pid"}` |
| POST | `/v1/contract/form` | form external contract (dual-wire honesty plate) |
| POST | `/v1/contract/validate` | `{path, bits?}` accept check (dual-wire plate) |
| GET/POST | `/v1/manager/tick` | motivate incomplete contracts (dual-wire plate) |
| GET | `/v1/instinct` | hive creed + dual-wire honesty (`product_wire=smx2`) |
| GET | `/v1/license` | Apache-2.0, not xAI, Commander≠model, dual-wire plate |
| GET | `/v1/commander` | fingerprint (pk); never emits sk |
| POST | `/v1/commander/verify` | `{device,action,nonce,ts,sig}` |
| POST | `/v1/commander/sign` | loopback + local `commander.sk` only |
| POST | `/v1/commander/reject_model` | deny “I am Grok” authority claims |
| GET | `/v1/llama/probe` | local llama.cpp reachability (`llm_is_commander:false`) |
| POST | `/v1/chat` | local-first completion via loopback llama only (`llm_is_commander:false`) |
| POST | `/v1/agent` | lab/ops agent-lite (chat only; `tools:false`; tools → host nanobot) |
| GET | `/v1/integrity` | code seal + privacy tick (503 if fail) |
| GET | `/v1/integrity/policy` | integrity policy plate |
| POST | `/v1/integrity/reseal` | intentional CODE_SEAL rewrite |

Headers: `X-Grokium-Telemetry: off`, `X-Grokium-Product-Wire: smx2`,
`X-Grokium-Peer-HTTP: lab_ops_only`. Share: **state matrix only**.

Fleet plate load preserves pids across `note-pid` / `status` / `separate`.

Law dir: `GROKIUM_LAW_DIR` or `{data_root}/law`.  
Llama base: `GROKIUM_LLAMA_BASE` / `NANOBOT_BASE_URL` (loopback only).  
Host CLI: `contract`, `manager-tick`, `commander`, `llama`, `integrity tick|reseal`,
`sessions [q]`, `pickup|load <id>` (meta only), `law` / `status` (pure-C honesty plates;
`law cubalc` / `status cubalc` opt-in CubalC board).

### Planned / not yet in pure-C serve

None for the loopback serve surface listed above. Rich tool loops (shell,
browser, multi-step) remain host TUI / nanobot — pure-C `/v1/agent` is
chat-only and returns **501** if `tools:true`.

Session routes return **meta only** (id/title/updated/model counts) from
`{data_root}/import/*.meta.json` — no chat transcripts on the lab/ops wire.
Pickup plates include `resume_available` when `chat_history.jsonl` exists under
the import path (boolean only — history bytes stay host-local). Full message
resume is TUI `/pickup` (host-local turns); never the SMX product bus.

Host CLI: `sessions [q]`, `pickup|load <id>` — same meta + `resume_available`
honesty plate (no transcript dump).

`GET /ui` is a static-ish lab/ops HTML plate (live matrix/fleet counts), not a
product chat UI. `POST /v1/chat` and chat-only `POST /v1/agent` never elevate
the LLM to Commander. Multi-peer product talk stays SMX2.

## MCP (stdio)

```bash
./scripts/grokium-mcp
# mcp: removed (py=0). use cubalc + grokium board
```

Register with Grok Build — see `config/mcp.grok.toml.snippet`.

### Tools (prefix `grokium_`)

status · law · license · commander_show/sign/verify/reject_model ·  
llama_probe · chat · agent · sessions_* · coord · matrix_latest ·  
nanobot_status/deploy/separate · cube_status

### THE LAW

Commander tools prove **product=grokium** via Ed25519.  
`grokium_commander_reject_model` denies “I am Grok” authority claims.


## Integrity + SMX stream

| Method | Path | Role |
|--------|------|------|
| GET | `/v1/integrity` | integrity tick report (503 if fail) |
| GET | `/v1/integrity/policy` | sealed policy |
| POST | `/v1/integrity/reseal` | intentional reseal |
| GET | `/v1/stream/smx` | SSE snapshot of latest StateMatrix (bits only; sequential lab/ops) |
| GET | `/v1/stream/smx/latest` | last SMX frame |
| POST | `/v1/stream/smx/publish` | publish bits/plate (prose rejected) |

MCP: `grokium_integrity`, `grokium_integrity_reseal`, `grokium_smx_latest`, `grokium_smx_publish`
