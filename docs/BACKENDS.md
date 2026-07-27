## Auth tokens (optional Grok cloud)

 (same token store as original CLI)

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


## Local model selection

See [USER_GUIDE.md](USER_GUIDE.md) and `config/models.toml`.

```bash
./scripts/grokium models list
./scripts/grokium models set qwen
# TUI: /model list  ·  /model qwen
```


## Failover / backup (session-safe)

When primary backend is down, Grokium can try the other (default **auto**):

```text
/failover auto              # active first, other as backup
/failover none              # no backup
/failover local_then_grok
/failover grok_then_local
```

```bash
export GROKIUM_FAILOVER=auto
```

### `/model` shows both

```text
/model              # list local llama + grok, ★ = active
/model local        # switch to llama.cpp — chat history kept
/model grok         # switch to Grok — chat history kept
/model qwen         # local alias
```

Switching **does not** run `/new` — conversation continues on the new backend.
