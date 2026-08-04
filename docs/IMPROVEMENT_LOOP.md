# Continuous improvement loop

A durable scheduled agent runs **every 13 minutes**:

- Analyze logical gaps (Cube / Hive Mind / pure-C integrity)
- Land **one** focused fix
- Commit (and push when clean)

Scheduler task id is session-local; recreate with Grok if needed.

Manual first-cycle tools:

```bash
make -C c_core hive
./build/grokium-consolidate selftest
./scripts/hive/manifest_prophecy.sh
```
