# Backends: llama.cpp vs Grok auth

## Switch (TUI)

```
/backend local     # llama.cpp OpenAI API (default)  http://127.0.0.1:1212/v1
/backend grok      # cloud Grok — opt-in
/backend           # show status
/model             # show model ids
```

## Env for Grok

```bash
export GROK_API_KEY=...    # or XAI_API_KEY
./scripts/grokium          # TUI
# then: /backend grok
```

Or enable in config (still needs key):

```toml
[auth]
enabled = true
base_url = "https://cli-chat-proxy.grok.com/v1"
model = "grok-4.5"
```

## Rules

- **Default = local** (llama.cpp)
- **No silent fallback** between cores (unmixed)
- **Grok is not commander** — only Ed25519 Grokium product identity is
- Telemetry stays off

## Nanobot

**Not the chat core.** Chat/agent = Grokium `llm.py` + tools.  
Nanobot = integrity core + optional fleet peers (`/fleet`, `/integrity`).
