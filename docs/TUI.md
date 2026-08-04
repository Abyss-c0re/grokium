# Grokium TUI (primary)

**The TUI is the main UI.** Web is optional (`grokium serve`).

```bash
./scripts/grokium
./scripts/grokium tui
```

## Modes
| Mode | How |
|------|-----|
| chat | `/mode chat` — tools off |
| agent | `/mode agent` — tools on |
| resume | `/mode resume` honesty + `/pickup <id>` meta only |

## Slash commands
`/help` `/settings` `/model` `/backend` `/clear` `/quit`  
`/status` — dual-wire honesty plate (fleet kill(0) + matrix bits; SMX2 ≠ peer HTTP)  
`/hub [start|stop]` — LLM request hub (sched status)  
`/coord <NEXUS_COORD|01-bits>` — fold plate via SMX filter (fail-closed; prose denied)  
`/smx` (or `/matrix`) — latest StateMatrix plate (`data/matrix/LATEST.json` or ability)  
`/sessions [q]` — imported session **metas** only (`data/import/*.meta.json`)  
`/pickup` `/load` `<id>` — meta pickup (no transcript dump)  
`/mode chat|agent|resume` — tools toggle; resume stays meta-only on TUI wire  
`/law` — Cube Standards plate (share=state_matrix_only; dual-wire honesty)  
`/fleet [status|defaults|deploy|spawn …|cubalc]` — pure-C plate (honest pid/status)  
`/integrity` — CODE_SEAL + privacy fail-closed tick  
`/commander` — Ed25519 law fingerprint (never a Grok model)  

Product bus remains **SMX2**; peer HTTP = lab/ops only; Commander ≠ model; share = state_matrix_only.  
Session resume of full messages stays host/nanobot path — not the product bus.

## Optional web
```bash
./scripts/grokium serve   # API + browser UI on :17444
```
