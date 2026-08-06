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
| agent | `/mode agent` — tools on (default for long work) |
| resume | `/mode resume` + `/pickup <id>` host-local history (not SMX bus) |

## Keys (chat UX)
| Key | Action |
|-----|--------|
| **Enter** | Send |
| **Shift+Enter** / **Alt+Enter** | Newline (when multiline on) |
| **Tab** | Complete `/` command · or cycle spoilers when empty |
| **PgUp / PgDn** · **mouse wheel** | Scroll transcript |
| **Ctrl+S** | Send |

Type `/` for live slash hints (`settings`, `agents`, `fleet`, `auth`, …).

## Slash commands
`/help` `/settings` `/model` `/backend` `/clear` `/quit`  
`/agents` `/subagent` `/nanobots` — tools · subagents · fleet · long-task surface  
`/status` — dual-wire honesty plate (fleet kill(0) + matrix bits; SMX2 ≠ peer HTTP)  
`/hub [start|stop]` — LLM request hub (sched status)  
`/coord <NEXUS_COORD|01-bits>` — fold plate via SMX filter (fail-closed; prose denied)  
`/smx` (or `/matrix`) — latest StateMatrix plate (`data/matrix/LATEST.json` or ability)  
`/sessions [q]` — imported session **metas** (`data/import/*.meta.json`)  
`/pickup` `/load` `<id>` — meta + host-local `chat_history.jsonl` into TUI (last turns)  
`/mode chat|agent|resume` — tools toggle; resume is host-local only (not product bus)  
`/law` — Cube Standards plate (share=state_matrix_only; dual-wire honesty)  
`/license` — Apache-2.0 · not xAI · Commander≠model dual-wire plate  
`/auth` `/auth import` — status · seal credentials from `~/.grok` when present  
`/login` — optional cloud opt-in via installed CLI, then import  

`/fleet [status|defaults|deploy|save|spawn|note-pid|separate|stop-all|cubalc]` — pure-C plate (honest pid/status)  
`/manager [DIR]` — motivate incomplete contracts (nb-manager / SMX2; `help`/`?` = dual-wire plate)  
`/contract form|validate|manager-tick …` — external cell contracts (SMX filter)  
`/integrity` — CODE_SEAL + privacy fail-closed tick  
`/commander` — Ed25519 law fingerprint (never a Grok model)  

### Perfect assistant (product vision)
- **No stream spam** — delta-first SSE · suffix-only · emit caps · never re-append full reply  
- **Human META** — dual-wire plates render as one-liners (`· ready …`); `/debug` shows raw JSON  
- **Markdown** — light `**bold**` / `` `code` `` on assistant rows  
- **Both cores tool-call** — local llama (`NANOBOT_LOCAL_TOOLS`) and Grok cloud  
- **Long tasks** — high turn budget · task board stretches · shell/subagents/braincells  
- **Nanobots** — `/fleet` deploy/manage homes · agent-spawned explore/plan/general  

Product bus remains **SMX2**; peer HTTP = lab/ops only; Commander ≠ model; share = state_matrix_only.  
Machine dual-wire plates from pure-C tools (`"schema":"grokium.*"`) are **humanized** in the
TUI log; free-form JSON dumps stay hidden unless `/debug` is on.  
Session resume loads user/assistant turns into the local TUI and seeds nanobot
recent memory (host-local) so the next agent turns have context — never the SMX
product bus.

## Optional web
```bash
./scripts/grokium serve   # API + browser UI on :17444
```
