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
| GET | `/v1/status` | law + dual-wire honesty (`product_wire=smx2`) |
| GET | `/v1/law` | law plate |
| GET | `/v1/ability` | consolidator ability card |
| POST | `/v1/coord` | NEXUS_COORD → matrix (**SMX filter sanitize**) |
| POST | `/v1/stream/smx/publish` | same as coord |
| GET | `/v1/matrix/latest` | last SMX |
| GET | `/v1/stream/smx/latest` | same as matrix/latest |
| GET | `/v1/nanobot/status` | fleet plate (honest pid/status) |
| POST | `/v1/nanobot/deploy` | deploy homes + FLEET.json |
| POST | `/v1/nanobot/spawn` | fork/exec bot(s); body = id, `{"id":…}`, or empty=all |
| POST | `/v1/nanobot/separate` | SIGTERM one bot; body = id or `{"id":…}` |
| POST | `/v1/contract/form` | form external contract JSON body |
| POST | `/v1/contract/validate` | `{path, bits?}` accept check |
| GET/POST | `/v1/manager/tick` | motivate incomplete contracts |
| GET | `/v1/instinct` | hive mind creed line |
| GET | `/v1/license` | Apache-2.0 + not affiliated with xAI |
| GET | `/v1/commander` | fingerprint (pk); never emits sk |
| POST | `/v1/commander/verify` | `{device,action,nonce,ts,sig}` |
| POST | `/v1/commander/sign` | loopback + local `commander.sk` only |
| POST | `/v1/commander/reject_model` | deny “I am Grok” authority claims |

Headers: `X-Grokium-Telemetry: off`, `X-Grokium-Product-Wire: smx2`,
`X-Grokium-Peer-HTTP: lab_ops_only`. Share: **state matrix only**.

Fleet plate load preserves pids across `note-pid` / `status` / `separate`.

Law dir: `GROKIUM_LAW_DIR` or `{data_root}/law`.  
Host CLI: `grokium contract …`, `manager-tick`, `commander show|sign|verify|…`.

### Planned / not yet in pure-C serve

| Method | Path | Role |
|--------|------|------|
| GET | `/v1/llama/probe` | local llama |
| POST | `/v1/chat` | local-first chat (host TUI path today) |
| POST | `/v1/agent` | tool agent |
| GET/POST | `/v1/sessions/*` | search/pickup/resume/import |
| GET | `/v1/cube/status` | Cube bridge |
| GET | `/ui` | minimal UI |
| GET | `/v1/integrity*` | integrity tick / reseal |
| GET | `/v1/stream/smx` | SSE stream |

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
| GET | `/v1/stream/smx` | SSE real-time StateMatrix (bits only) |
| GET | `/v1/stream/smx/latest` | last SMX frame |
| POST | `/v1/stream/smx/publish` | publish bits/plate (prose rejected) |

MCP: `grokium_integrity`, `grokium_integrity_reseal`, `grokium_smx_latest`, `grokium_smx_publish`
