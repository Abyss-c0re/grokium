# Wire compatibility fixtures

Machine plates only — no prose chat samples as product bus.

| Fixture | Role | Expect |
|---------|------|--------|
| `nexus_coord.v1.plate` | Valid `NEXUS_COORD` machine plate | filter allow |
| `nexus_coord.smuggle.plate` | Prefix + chat smuggle | filter deny |
| `nexus_coord.hold_flash0.plate` | Machine plate with `hold_flash=0` | filter deny |
| `smx.bits.v1.plate` | Pure 01 StateMatrix lattice (≥32 bits) | filter allow |
| `prose.chat.deny.txt` | Free-form chat / injection prose | filter deny (`prose`) |
| `commander.reject_model.txt` | Model authority claim | commander reject |

```bash
make -C c_core all
./build/grokium-smx-filter allow-check "$(cat compat/nexus_coord.v1.plate)"
./build/grokium-smx-filter allow-check "$(cat compat/smx.bits.v1.plate)"
# expect allow:true

./build/grokium-smx-filter allow-check "$(cat compat/nexus_coord.smuggle.plate)"
./build/grokium-smx-filter allow-check "$(cat compat/nexus_coord.hold_flash0.plate)"
./build/grokium-smx-filter allow-check "$(cat compat/prose.chat.deny.txt)"
# expect allow:false

# hive target runs the full fixture set
make -C c_core hive
```
