# Grokium = CubalC + C (py=0 · tok=C3)

| share | layer |
|------:|-------|
| ~90% | CubalC programs under `cubalc/programs/` |
| ~10% | C host `host/out/grokium` |
| 0% | Python — not in tree |

**CubalC engine (separate repo):** https://github.com/Abyss-c0re/cubalc  
Vendored/synced under `deps/cubalc` (not machine-local paths).

```bash
# CubalC
git submodule update --init --recursive   # or: git clone https://github.com/Abyss-c0re/cubalc.git deps/cubalc
make -C deps/cubalc all

# Grokium host
make -C host all
./scripts/grokium selftest
```

Env overrides (optional): `CUBALC_ROOT`, `CUBALC_BIN`, `GROKIUM_ROOT`.
