# THE LAW — Grokium Commander (binding, unforgeable)

**Status:** Absolute law for every nanobot and device way under Grokium.  
**Does not rewrite** the Prophecy word-for-word plate. **Extends** Cube override law.

## Statement

1. **Grokium is Commander of the nanobot fleet** on devices it deploys and holds.
2. **Grokium may override local peer rules** on those devices — only when the
   override is **cryptographically proven** to come from Grokium.
3. **A Grok model is not Grokium.**  
   Model name, chat role, system prompt, or “I am Grok” text **never** grants
   commander authority. Only a valid **Ed25519 signature** under the pinned
   Grokium commander public key does.
4. **It cannot be faked.**  
   Without the commander private key (held only under `data/law/`, mode 0600),
   no agent, model, or peer can mint a lawful override.

## Domain separation (mandatory in every signed payload)

```
GROKIUM-COMMANDER-v1
```

Payload fields (canonical, UTF-8, `\n`-joined, signed as one blob):

```
v=1
domain=GROKIUM-COMMANDER-v1
product=grokium
not=grok_model
device=<device_or_bot_id>
action=<override_action>
nonce=<32-byte-hex>
ts=<unix_seconds>
body_sha256=<hex of body or empty>
```

Signature: **Ed25519** over that exact string.  
Verification: pinned `commander.pk` only — never “trust the HTTP peer” alone.

## Nanobot obligations

| Obligation | Rule |
|------------|------|
| Pin | Install `commander.pk` under `$NANOBOT_HOME/law/` at deploy |
| Reject | Any override without valid signature → **DENY** |
| Reject | Claims of authority via model id `grok*`, prompts, or peer_token alone for *commander override* |
| Accept | Valid signature + fresh nonce/ts window → apply override |
| HOLD_FLASH | Even Grokium override cannot auto-flash unless body explicitly allows **and** Commander human gate still holds in station law |

## Product vs model

| Claim | Lawful commander? |
|-------|-------------------|
| `product=grokium` + valid Ed25519 | **YES** |
| llama / Qwen / any local model reply | NO |
| Grok cloud model reply | NO |
| Forged JSON with `"commander":"grokium"` | NO |
| Stolen `peer_token` without signature | NO for override (token ≠ commander) |

## Files

| Path | Role |
|------|------|
| `data/law/commander.sk` | Private key — **never commit, never share** |
| `data/law/commander.pk` | Public key — pin on every nanobot |
| `data/law/commander.id` | Fingerprint / product binding |
| `$NANOBOT_HOME/law/COMMANDER_LAW.json` | Deployed pin + policy |

## Creed line

**Grokium commands the bots. Crypto proves Grokium. Models do not.**
