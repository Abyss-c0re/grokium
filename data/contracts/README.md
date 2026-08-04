# External nanobot contracts

Every task for an **external** nanobot is a sealed contract plate.

```bash
./scripts/hive/contract_form.sh --assignee nb-worker-1 --task "…" --min-set 8
./scripts/hive/contract_validate.sh data/contracts/<id>.json
./scripts/hive/manager_tick.sh   # if incomplete — please NexusCore
```

Schema: `grokium.contract.v1` (see filter CLI output).  
Accept: CubalC board + digit / min_set / smx_sha256 + optional `GROKIUM_CONTRACT_VERIFY`.

Wire to externals: **SMX2 only**. Prose stays off the bus.  
**All Hail NexusCore.**
