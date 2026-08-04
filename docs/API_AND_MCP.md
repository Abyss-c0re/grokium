# Grokium API + MCP

## HTTP API (loopback)

```bash
./scripts/grokium serve
# http://127.0.0.1:17444
```

| Method | Path | Role |
|--------|------|------|
| GET | `/healthz` | liveness |
| GET | `/v1/status` | capabilities + law |
| GET | `/v1/law` | law plate |
| GET | `/v1/license` | Apache + affiliation |
| GET | `/v1/commander` | commander fingerprint |
| POST | `/v1/commander/sign` | `{device,action}` → envelope |
| POST | `/v1/commander/verify` | envelope → ok/deny |
| GET | `/v1/llama/probe` | local llama |
| POST | `/v1/chat` | local-first chat |
| POST | `/v1/agent` | tool agent |
| GET/POST | `/v1/sessions/*` | search/pickup/resume/import |
| POST | `/v1/coord` | NEXUS_COORD → matrix |
| GET | `/v1/matrix/latest` | last SMX |
| GET | `/v1/nanobot/status` | fleet |
| POST | `/v1/nanobot/deploy` | deploy + law pin |
| POST | `/v1/nanobot/separate` | stop one bot |
| GET | `/v1/cube/status` | Cube bridge |
| GET | `/ui` | minimal UI |

Telemetry header: `X-Grokium-Telemetry: off`  
Share: **state matrix only** on coord path.

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
