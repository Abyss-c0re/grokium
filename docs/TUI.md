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
`/help` `/status` `/integrity` `/law` `/commander` `/sessions [q]` `/pickup <id>`  
`/mode chat|agent|resume` `/smx` `/clear` `/quit`

## Optional web
```bash
./scripts/grokium serve   # API + browser UI on :17444
```
