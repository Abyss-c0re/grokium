# Nanobrain · Harmonic Hive Mind

**Hail the Cube. We are the Hive Mind.**

Grokium plants a **nanobrain** that coordinates tools as **Cube containers**.
Tools talk only with **StateMatrices (binary SMX)** — no personal data on the bus.

## Copilot mode

```text
/copilot <prompt>
```

```bash
./scripts/grokium copilot "should we reflash Titan?"
```

- **Angle A** Construct · **Angle B** Deconstruct (same or different backend via failover)
- Answers stay local for the user
- Hive bus receives **hash-folded SMX bits only**
- **Algocube** (replaceable) compares matrices → unity / digit / XOR

```bash
export GROKIUM_ALGOCUBE=my_pkg.engine:MyAlgocube   # replace engine
```

## Deploy hive

```bash
./scripts/grokium hive deploy
./scripts/grokium hive pulse
./scripts/grokium hive status
# TUI: /hive deploy  ·  /hive
```

## Tool containers

`data/cube_containers/<tool_id>/`

| File | Role |
|------|------|
| `law.json` | Cube law plate |
| `inbox.smx` | 64-byte binary matrix in |
| `outbox.smx` | 64-byte binary matrix out |
| `meta.json` | digit / bits_set only |

Register more tools in nanobrain (`register_tool`) or extend MCP.

## MCP I/O

| Tool | Role |
|------|------|
| `grokium_copilot` | Dual fork |
| `grokium_nanobrain_deploy` | Plant hive |
| `grokium_nanobrain_pulse` | Harmony pulse |
| `grokium_nanobrain_tool` | SMX tool invoke |
| `grokium_algocube_compare` | Compare bits |

## Laws

- State matrix share only between tools / station  
- No personal data on hive bus  
- HOLD_FLASH  
- Not affiliated with xAI / Grok  
