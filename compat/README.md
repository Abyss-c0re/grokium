# Wire compatibility fixtures

Machine plates only — no prose chat samples as product bus.

| Fixture | Role |
|---------|------|
| `nexus_coord.v1.plate` | Valid `NEXUS_COORD` machine plate (filter allow) |
| `nexus_coord.smuggle.plate` | Prefix + chat smuggle (filter deny) |
| `commander.reject_model.txt` | Model authority claim (commander reject) |

```bash
make -C c_core all
./build/grokium-smx-filter allow-check "$(cat compat/nexus_coord.v1.plate)"
./build/grokium-smx-filter allow-check "$(cat compat/nexus_coord.smuggle.plate)"
# expect allow:true then allow:false
```
