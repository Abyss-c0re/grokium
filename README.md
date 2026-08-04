# Grokium

Local-first coding agent host. **C + nanobot.** Not affiliated with xAI.

```bash
./scripts/sync_nanobot.sh
make host
./scripts/grokium              # TUI
./scripts/grokium -p 'hello'   # one-shot (llama.cpp :1212)
./scripts/grokium hub status   # shared LLM gate
```

## Layout

```
.agents/     Cube Laws + agent policy (tracked)
host/        C host — TUI, hub, config
config/      examples
scripts/     grokium entry
deps/        submodules: nanobot · braincube · cubalc
docs/        guides
cubalc/      optional board programs
```

**Brain architecture:** BrainCube (decision core) + nanobot **braincells** (explore / plan / implement) for coding tasks.
## Defaults

| Setting | Value |
|---------|--------|
| Backend | llama.cpp `http://127.0.0.1:1212/v1` |
| Product | `grokium` |
| Core | [nanobot](https://github.com/Abyss-c0re/nanobot) |
| Laws | [`.agents/`](.agents/) |

## Docs

- [User guide](docs/USER_GUIDE.md)
- [Developer](docs/DEVELOPER.md)
- [NOTICE](NOTICE) · [DISCLAIMER](DISCLAIMER.md) · [LICENSE](LICENSE)

## License

Apache-2.0. See `LICENSE` and `THIRD-PARTY-NOTICES`.
