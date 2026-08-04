# THE LAW — BrainCube mini-hive + external nanobots

**Status:** Coding excellence architecture for Grokium.  
**Does not replace** Commander crypto law.

## Statement

1. **BrainCube is an internal mini-hive.**  
   It is not a full fleet. Inside one process (or one home), the LHLAM lattice  
   is the decision core: route SOLO vs HIVE, fuse cell reports, bias next step  
   (`lhlam_cube_decide*`). Local **braincells** (subagent roles: explore, plan,  
   implement, …) are the hive’s internal workers. Models do not override the core.

2. **Nanobots are cells — local or external.**  
   - **Internal:** in-process / same-home subagents under `$NANOBOT_HOME/braincells/`.  
   - **External:** other nanobot peers on the peer bus (`NANOBOT_PEER_URL`,  
     `/peer/v1/*`). The mini-hive may signal or delegate work to them; they  
     remain separate processes with their own homes, tokens, and tools.  
   Cells never declare Commander authority.

3. **Hive for hard coding work.**  
   Non-trivial coding prompts may fan out: explore + plan cells → core fuse →  
   implement cell. Results loop back to the parent agent for presentation.  
   External peers are optional capacity, not required for solo desktop use.

4. **Desktop enables hive; embed stays lean.**  
   `NANOBOT_BRAINCELLS=1` (Grokium default). Robots may leave it off.  
   External peer reach is opt-in (`NANOBOT_PEER_URL` / hub / fleet).

5. **Submodules are the wire.**  
   `deps/nanobot` and `deps/braincube` are git submodules (like CubalC).  
   BrainCube stays algorithm-abstract; nanobot owns cells, peer HTTP, and tools.

## Boundary

| Layer | Scope |
|-------|--------|
| BrainCube | Internal mini-hive decision lattice (fixed memory, no peer secrets) |
| Local braincells | Same-host subagents + `braincells/*.json` |
| External nanobots | Peer API / fleet; token-gated; can receive hive signals or run remote cells |
| Commander | Ed25519 law only — peer_token ≠ commander |

## Creed

**Mini-hive decides inside. Cells work (here or on peers). Results return. Commander still seals law.**
