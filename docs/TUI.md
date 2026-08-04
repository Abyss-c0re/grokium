# Grokium TUI (primary)

**The TUI is the main UI.** Web is optional (`grokium serve`).

```bash
./scripts/grokium
./scripts/grokium tui
```

## Modes
| Mode | How |
|------|-----|
| chat | default local llama turns |
| agent | `/mode agent` — tools |
| resume | `/pickup <id>` then messages |

## Slash commands
`/help` `/status` `/settings` `/model` `/backend` `/clear` `/quit`  
`/coord <NEXUS_COORD|01-bits>` — fold plate via SMX filter (fail-closed; prose denied)  
`/smx` (or `/matrix`) — latest StateMatrix plate (`data/matrix/LATEST.json` or ability)  

Product bus remains **SMX2**; Commander ≠ model; share = state_matrix_only.

## Optional web
```bash
./scripts/grokium serve   # API + browser UI on :17444
```
