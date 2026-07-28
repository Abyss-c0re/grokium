# Nanobot fleet (external product)

Grokium **commands** nanobot peers. It does **not** vendor the C binary.

| Piece | Location |
|-------|----------|
| Nanobot SoT | https://github.com/Abyss-c0re/nanobot |
| Pin | [`NANOBOT_PIN.txt`](../NANOBOT_PIN.txt) |
| Fleet code | [`src/grokium/nanobots.py`](../src/grokium/nanobots.py) |
| Config | `[nanobot]` in [`config/grokium.toml`](../config/grokium.toml) |
| HTTP | `GET /v1/nanobot/status`, `POST /v1/nanobot/deploy`, `POST /v1/nanobot/separate` |

## Install nanobot (required for fleet)

```bash
git clone https://github.com/Abyss-c0re/nanobot.git
cd nanobot
git checkout v0.5.1   # or tip from NANOBOT_PIN.txt
make host
install -m755 build/host/nanobot ~/.local/bin/nanobot
nanobot --version     # expect 0.5.1+
```

Or Docker: `make docker` → `nanobot:0.5.1`.

## Wire to Grokium

```toml
# config/grokium.toml
[nanobot]
enabled = true
binary = ""   # empty → PATH / NANOBOT_BINARY / ~/.local/bin/nanobot
home_root = "data/home"
base_port = 28800
```

```bash
export NANOBOT_BINARY=$HOME/.local/bin/nanobot   # optional override
./scripts/grokium serve
# API: curl -s http://127.0.0.1:17444/v1/nanobot/status
```

Without a binary, fleet APIs return errors / empty status — chat/TUI still work with local llama.
